/**
 * @file test_ibl.cpp
 * @brief Tests for IBL cubemap↔equirectangular conversion and HDR/LDR saving.
 *
 * Validates:
 *   - E2C (equirect → cubemap) compute shader produces valid cubemap
 *   - C2E (cubemap → equirect) compute shader roundtrip matches input
 *   - Cubemap 6-face readback and .hdr saving (Radiance format)
 *   - Cubemap 6-face .png saving (LDR)
 *   - Direct HDR float image saving via ImageData::SavePixelDataHDR
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "asset/ImageData.h"
#include "render/ComputePipelineBuilder.h"
#include "render/DescriptorManager.h"
#include "render/Image.h"
#include "render/shaders/ShaderModule.h"

// --- Embedded shaders ---
#include <e2c.comp.h>
#include <c2e.comp.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

// Work around MSVC min/max macros from Windows.h
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

using namespace neurus;

// ---------------------------------------------------------------------------
// Reference output directory (relative to test working dir = build/debug/test/)
// ---------------------------------------------------------------------------
static const char* kReferenceDir = "../../../test/render/reference/ibl/";

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class IBLConversionTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kEquiWidth  = 128;
	static constexpr uint32_t kEquiHeight = 64;
	static constexpr uint32_t kCubeFaceRes = 32;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& dev = *m_device;
		auto& pd  = PhysicalDevice();

		// --- Samplers ---
		{
			vk::SamplerCreateInfo samplerCI(
				{}, vk::Filter::eLinear, vk::Filter::eLinear,
				vk::SamplerMipmapMode::eNearest,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				0.0f, VK_FALSE, 0.0f, VK_FALSE,
				vk::CompareOp::eAlways, 0.0f, 0.0f,
				vk::BorderColor::eFloatTransparentBlack, VK_FALSE);
			m_equirectSampler = vk::raii::Sampler(dev, samplerCI);
			m_cubemapSampler  = vk::raii::Sampler(dev, samplerCI);
		}

		// --- Cubemap Image (E2C output / C2E input) ---
		m_cubemapImage = std::make_unique<Image>(
			dev, pd,
			vk::Extent2D{kCubeFaceRes, kCubeFaceRes},
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eStorage |
			    vk::ImageUsageFlagBits::eSampled |
			    vk::ImageUsageFlagBits::eTransferSrc,
			/*mipLevels=*/1,
			Image::ImageType::eCube,
			"IBLTest_Cubemap");

		// --- Output equirect Image (C2E output, for readback) ---
		m_outputEquirect = std::make_unique<Image>(
			dev, pd,
			vk::Extent2D{kEquiWidth, kEquiHeight},
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eStorage |
			    vk::ImageUsageFlagBits::eTransferSrc,
			/*mipLevels=*/1,
			Image::ImageType::e2D,
			"IBLTest_OutEquirect");

		// --- E2C pipeline (equirect → cubemap) ---
		createE2CPipeline(dev);

		// --- C2E pipeline (cubemap → equirect) ---
		createC2EPipeline(dev);
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	// -------------------------------------------------------------------
	// Pipeline creation
	// -------------------------------------------------------------------

	void createE2CPipeline(const vk::raii::Device& dev)
	{
		auto bindings = BuildLayout()
			.AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
			            vk::ShaderStageFlagBits::eCompute)
			.AddBinding(1, vk::DescriptorType::eStorageImage,
			            vk::ShaderStageFlagBits::eCompute)
			.Build();

		m_e2cSetLayout = std::make_unique<DescriptorSetLayout>(dev, bindings);

		auto poolSizes = DescriptorPool::CalculatePoolSizes({m_e2cSetLayout.get()}, 1);
		m_e2cPool = std::make_unique<DescriptorPool>(dev, 1, poolSizes);
		m_e2cSets = m_e2cPool->Allocate(*m_e2cSetLayout, 1);

		auto compModule = ShaderModule::FromEmbedded(dev, e2c_comp_spv, sizeof(e2c_comp_spv));
		m_e2cBuilder = std::make_unique<ComputePipelineBuilder>(dev);
		m_e2cPipeline = m_e2cBuilder->SetShaderStage(compModule, "main")
			.AddDescriptorSetLayout(*m_e2cSetLayout->layout())
			.BuildComputePipeline();
	}

	void createC2EPipeline(const vk::raii::Device& dev)
	{
		auto bindings = BuildLayout()
			.AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
			            vk::ShaderStageFlagBits::eCompute)
			.AddBinding(1, vk::DescriptorType::eStorageImage,
			            vk::ShaderStageFlagBits::eCompute)
			.Build();

		m_c2eSetLayout = std::make_unique<DescriptorSetLayout>(dev, bindings);

		auto poolSizes = DescriptorPool::CalculatePoolSizes({m_c2eSetLayout.get()}, 1);
		m_c2ePool = std::make_unique<DescriptorPool>(dev, 1, poolSizes);
		m_c2eSets = m_c2ePool->Allocate(*m_c2eSetLayout, 1);

		auto compModule = ShaderModule::FromEmbedded(dev, c2e_comp_spv, sizeof(c2e_comp_spv));
		m_c2eBuilder = std::make_unique<ComputePipelineBuilder>(dev);
		m_c2ePipeline = m_c2eBuilder->SetShaderStage(compModule, "main")
			.AddDescriptorSetLayout(*m_c2eSetLayout->layout())
			.BuildComputePipeline();
	}

	// -------------------------------------------------------------------
	// Procedural equirectangular gradient generation
	// -------------------------------------------------------------------

	static std::vector<float> GenerateEquirectGradient(uint32_t width, uint32_t height)
	{
		std::vector<float> pixels(static_cast<size_t>(width) * height * 4, 0.0f);
		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
				const float u = static_cast<float>(x) / static_cast<float>(width);
				const float v = static_cast<float>(y) / static_cast<float>(height);
				const float brightness = 1.0f - v;
				const float warm = std::max(0.0f, (0.5f - u) * 2.0f);
				const float cool = std::max(0.0f, (u - 0.5f) * 2.0f);

				pixels[idx + 0] = (warm * 1.0f + cool * 0.2f) * brightness;
				pixels[idx + 1] = (warm * 0.4f + cool * 0.7f) * brightness;
				pixels[idx + 2] = (warm * 0.1f + cool * 1.0f) * brightness;
				pixels[idx + 3] = 1.0f;
			}
		}
		return pixels;
	}

	// -------------------------------------------------------------------
	// Image upload helpers
	// -------------------------------------------------------------------

	std::unique_ptr<Image> createAndUploadEquirect(const std::vector<float>& pixelData,
	                                                uint32_t width,
	                                                uint32_t height,
	                                                const char* debugName)
	{
		auto& dev = *m_device;
		auto& pd  = PhysicalDevice();

		auto img = std::make_unique<Image>(
			dev, pd, vk::Extent2D{width, height},
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eSampled |
			    vk::ImageUsageFlagBits::eTransferDst |
			    vk::ImageUsageFlagBits::eStorage,
			/*mipLevels=*/1,
			Image::ImageType::e2D,
			debugName);

		uploadPixelsToImage(*img, pixelData.data(),
		                    static_cast<vk::DeviceSize>(pixelData.size() * sizeof(float)),
		                    width, height);
		return img;
	}

	void uploadPixelsToImage(Image& img, const void* data, vk::DeviceSize dataSize,
	                         uint32_t width, uint32_t height)
	{
		auto& dev = *m_device;
		auto& pd  = PhysicalDevice();

		// Staging buffer
		vk::BufferCreateInfo stagingCI({}, dataSize, vk::BufferUsageFlagBits::eTransferSrc);
		vk::raii::Buffer stagingBuf(dev, stagingCI);

		auto memReqs = stagingBuf.getMemoryRequirements();
		const uint32_t memType = findMemoryType(pd, memReqs.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent);
		vk::raii::DeviceMemory stagingMem(dev, vk::MemoryAllocateInfo(memReqs.size, memType));
		stagingBuf.bindMemory(*stagingMem, 0);

		void* mapped = stagingMem.mapMemory(0, dataSize);
		std::memcpy(mapped, data, static_cast<size_t>(dataSize));
		stagingMem.unmapMemory();

		auto& cmd = BeginCmd();

		// Undefined → TransferDst
		vk::ImageMemoryBarrier preBarrier(
			vk::AccessFlagBits::eNone,
			vk::AccessFlagBits::eTransferWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*img.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
		                    vk::PipelineStageFlagBits::eTransfer,
		                    {}, {}, {}, {preBarrier});

		vk::BufferImageCopy copyRegion(0, 0, 0,
			vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
			vk::Offset3D(0, 0, 0),
			vk::Extent3D(width, height, 1));
		cmd.copyBufferToImage(*stagingBuf, *img.ImageHandle(),
		                       vk::ImageLayout::eTransferDstOptimal, {copyRegion});

		// TransferDst → ShaderReadOnly
		vk::ImageMemoryBarrier postBarrier(
			vk::AccessFlagBits::eTransferWrite,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*img.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                    vk::PipelineStageFlagBits::eComputeShader,
		                    {}, {}, {}, {postBarrier});

		EndSubmitWait(cmd);
		img.SetCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
	}

	// -------------------------------------------------------------------
	// Conversion dispatch
	// -------------------------------------------------------------------

	/**
	 * @brief Runs E2C: equirect → cubemap.
	 */
	void runE2C(const Image& equirectSrc, Image& cubeDst)
	{
		// Write descriptors (both bindings on set 0)
		{
			vk::DescriptorImageInfo srcInfo(*m_equirectSampler,
			                                *equirectSrc.ImageViewHandle(),
			                                vk::ImageLayout::eShaderReadOnlyOptimal);
			m_e2cSets[0].WriteImage(0, srcInfo, vk::DescriptorType::eCombinedImageSampler);

			vk::DescriptorImageInfo dstInfo(nullptr, *cubeDst.ImageViewHandle(),
			                                vk::ImageLayout::eGeneral);
			m_e2cSets[0].WriteImage(1, dstInfo, vk::DescriptorType::eStorageImage);
		}

		auto& cmd = BeginCmd();

		// Transition cubemap: UNDEFINED → GENERAL
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eNone,
			vk::AccessFlagBits::eShaderWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eGeneral,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*cubeDst.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
		                    vk::PipelineStageFlagBits::eComputeShader,
		                    {}, {}, {}, {barrier});
		cubeDst.SetCurrentLayout(vk::ImageLayout::eGeneral);

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_e2cPipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
		                       *m_e2cBuilder->pipelineLayout(),
		                       0, {m_e2cSets[0].handle()}, {});

		const uint32_t gx = (kCubeFaceRes + 3) / 4;
		const uint32_t gy = (kCubeFaceRes + 3) / 4;
		cmd.dispatch(gx, gy, 6);

		// Memory barrier for cubemap write visibility
		vk::ImageMemoryBarrier memBarrier(
			vk::AccessFlagBits::eShaderWrite,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eGeneral,
			vk::ImageLayout::eGeneral,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*cubeDst.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
		                    vk::PipelineStageFlagBits::eComputeShader,
		                    {}, {}, {}, {memBarrier});

		EndSubmitWait(cmd);
	}

	/**
	 * @brief Runs C2E: cubemap → equirect.
	 */
	void runC2E(const Image& cubeSrc, Image& equiDst)
	{
		// Write descriptors (both bindings on set 0)
		{
			vk::DescriptorImageInfo srcInfo(*m_cubemapSampler,
			                                *cubeSrc.ImageViewHandle(),
			                                vk::ImageLayout::eGeneral);
			m_c2eSets[0].WriteImage(0, srcInfo, vk::DescriptorType::eCombinedImageSampler);

			vk::DescriptorImageInfo dstInfo(nullptr, *equiDst.ImageViewHandle(),
			                                vk::ImageLayout::eGeneral);
			m_c2eSets[0].WriteImage(1, dstInfo, vk::DescriptorType::eStorageImage);
		}

		auto& cmd = BeginCmd();

		// Transition output equirect: UNDEFINED → GENERAL
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eNone,
			vk::AccessFlagBits::eShaderWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eGeneral,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*equiDst.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
		                    vk::PipelineStageFlagBits::eComputeShader,
		                    {}, {}, {}, {barrier});
		equiDst.SetCurrentLayout(vk::ImageLayout::eGeneral);

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_c2ePipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
		                       *m_c2eBuilder->pipelineLayout(),
		                       0, {m_c2eSets[0].handle()}, {});

		const uint32_t gx = (kEquiWidth  + 15) / 16;
		const uint32_t gy = (kEquiHeight + 15) / 16;
		cmd.dispatch(gx, gy, 1);

		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Readback helpers
	// -------------------------------------------------------------------

	std::vector<float> readbackFloatImage(const Image& img,
	                                       vk::ImageLayout currentLayout,
	                                       uint32_t width, uint32_t height)
	{
		auto& dev = *m_device;
		auto& pd  = PhysicalDevice();

		const vk::DeviceSize bufSize = static_cast<vk::DeviceSize>(width) * height * 16;
		vk::BufferCreateInfo stagingCI({}, bufSize, vk::BufferUsageFlagBits::eTransferDst);
		vk::raii::Buffer stagingBuf(dev, stagingCI);

		auto memReqs = stagingBuf.getMemoryRequirements();
		const uint32_t memType = findMemoryType(pd, memReqs.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent);
		vk::raii::DeviceMemory stagingMem(dev, vk::MemoryAllocateInfo(memReqs.size, memType));
		stagingBuf.bindMemory(*stagingMem, 0);

		auto& cmd = BeginCmd();

		// Transition to TRANSFER_SRC if needed
		if (currentLayout != vk::ImageLayout::eTransferSrcOptimal)
		{
			vk::ImageMemoryBarrier transBarrier(
				vk::AccessFlagBits::eShaderWrite,
				vk::AccessFlagBits::eTransferRead,
				currentLayout,
				vk::ImageLayout::eTransferSrcOptimal,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				*img.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
			                    vk::PipelineStageFlagBits::eTransfer,
			                    {}, {}, {}, {transBarrier});
		}

		vk::BufferImageCopy copyRegion(0, 0, 0,
			vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
			vk::Offset3D(0, 0, 0),
			vk::Extent3D(width, height, 1));
		cmd.copyImageToBuffer(*img.ImageHandle(), vk::ImageLayout::eTransferSrcOptimal,
		                       *stagingBuf, {copyRegion});

		vk::MemoryBarrier memBarrier(vk::AccessFlagBits::eTransferWrite,
		                             vk::AccessFlagBits::eHostRead);
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                    vk::PipelineStageFlagBits::eHost,
		                    {}, {memBarrier}, {}, {});
		EndSubmitWait(cmd);

		std::vector<float> result(static_cast<size_t>(width) * height * 4);
		void* mapped = stagingMem.mapMemory(0, bufSize);
		std::memcpy(result.data(), mapped, static_cast<size_t>(bufSize));
		stagingMem.unmapMemory();
		return result;
	}

	std::vector<float> readbackCubemapFaces(const Image& cubeImg)
	{
		auto& dev = *m_device;
		auto& pd  = PhysicalDevice();
		const uint32_t faceRes = kCubeFaceRes;

		const vk::DeviceSize bufSize = static_cast<vk::DeviceSize>(faceRes) * faceRes * 6 * 16;
		vk::BufferCreateInfo stagingCI({}, bufSize, vk::BufferUsageFlagBits::eTransferDst);
		vk::raii::Buffer stagingBuf(dev, stagingCI);

		auto memReqs = stagingBuf.getMemoryRequirements();
		const uint32_t memType = findMemoryType(pd, memReqs.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eHostVisible |
			vk::MemoryPropertyFlagBits::eHostCoherent);
		vk::raii::DeviceMemory stagingMem(dev, vk::MemoryAllocateInfo(memReqs.size, memType));
		stagingBuf.bindMemory(*stagingMem, 0);

		auto& cmd = BeginCmd();

		// Transition to TRANSFER_SRC
		vk::ImageMemoryBarrier transBarrier(
			vk::AccessFlagBits::eShaderWrite,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageLayout::eGeneral,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*cubeImg.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
		                    vk::PipelineStageFlagBits::eTransfer,
		                    {}, {}, {}, {transBarrier});

		// Copy all 6 faces (layerCount = 6)
		vk::BufferImageCopy copyRegion(0, 0, 0,
			vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 6),
			vk::Offset3D(0, 0, 0),
			vk::Extent3D(faceRes, faceRes, 1));
		cmd.copyImageToBuffer(*cubeImg.ImageHandle(), vk::ImageLayout::eTransferSrcOptimal,
		                       *stagingBuf, {copyRegion});

		vk::MemoryBarrier memBarrier(vk::AccessFlagBits::eTransferWrite,
		                             vk::AccessFlagBits::eHostRead);
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                    vk::PipelineStageFlagBits::eHost,
		                    {}, {memBarrier}, {}, {});
		EndSubmitWait(cmd);

		std::vector<float> result(static_cast<size_t>(faceRes) * faceRes * 6 * 4);
		void* mapped = stagingMem.mapMemory(0, bufSize);
		std::memcpy(result.data(), mapped, static_cast<size_t>(bufSize));
		stagingMem.unmapMemory();
		return result;
	}

	// -------------------------------------------------------------------
	// Utility
	// -------------------------------------------------------------------

	static uint32_t findMemoryType(const vk::raii::PhysicalDevice& pd,
	                                uint32_t typeFilter,
	                                vk::MemoryPropertyFlags props)
	{
		auto memProps = pd.getMemoryProperties();
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1u << i)) &&
			    (memProps.memoryTypes[i].propertyFlags & props) == props)
			{
				return i;
			}
		}
		return 0;
	}

	static void ensureDir(const std::string& filePath)
	{
		const auto parent = std::filesystem::path(filePath).parent_path();
		if (!parent.empty() && !std::filesystem::exists(parent))
			std::filesystem::create_directories(parent);
	}

	// -------------------------------------------------------------------
	// Member state
	// -------------------------------------------------------------------

	vk::raii::Sampler m_equirectSampler = nullptr;
	vk::raii::Sampler m_cubemapSampler  = nullptr;

	std::unique_ptr<Image> m_cubemapImage;
	std::unique_ptr<Image> m_outputEquirect;

	// E2C
	std::unique_ptr<DescriptorSetLayout> m_e2cSetLayout;
	std::unique_ptr<DescriptorPool> m_e2cPool;
	std::vector<DescriptorSet> m_e2cSets;
	std::unique_ptr<ComputePipelineBuilder> m_e2cBuilder;
	vk::raii::Pipeline m_e2cPipeline = nullptr;

	// C2E
	std::unique_ptr<DescriptorSetLayout> m_c2eSetLayout;
	std::unique_ptr<DescriptorPool> m_c2ePool;
	std::vector<DescriptorSet> m_c2eSets;
	std::unique_ptr<ComputePipelineBuilder> m_c2eBuilder;
	vk::raii::Pipeline m_c2ePipeline = nullptr;
};

// ===========================================================================
// 1. E2C → C2E roundtrip test
// ===========================================================================

TEST_F(IBLConversionTest, E2C_C2E_Roundtrip_ProducesMatchingPixels)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Generate test pattern
	auto srcPixels = GenerateEquirectGradient(kEquiWidth, kEquiHeight);

	// Upload to GPU
	auto equiImg = createAndUploadEquirect(srcPixels, kEquiWidth, kEquiHeight,
	                                        "IBLTest_SrcEquirect");

	// E2C: equirect → cubemap
	runE2C(*equiImg, *m_cubemapImage);

	// C2E: cubemap → equirect
	runC2E(*m_cubemapImage, *m_outputEquirect);

	// Read back result
	auto resultPixels = readbackFloatImage(*m_outputEquirect,
	                                       vk::ImageLayout::eGeneral,
	                                       kEquiWidth, kEquiHeight);

	// Compare with tolerance (bilinear filtering in both directions causes precision loss)
	const float kTolerance = 0.15f;
	const size_t totalPixels = static_cast<size_t>(kEquiWidth) * kEquiHeight;
	size_t mismatched = 0;
	float maxDiff = 0.0f;

	for (size_t i = 0; i < totalPixels; ++i)
	{
		for (int c = 0; c < 3; ++c)
		{
			const float diff = std::abs(srcPixels[i * 4 + c] - resultPixels[i * 4 + c]);
			if (diff > kTolerance) { ++mismatched; maxDiff = std::max(maxDiff, diff); break; }
			maxDiff = std::max(maxDiff, diff);
		}
	}

	const float mismatchRate = static_cast<float>(mismatched) / static_cast<float>(totalPixels);
	EXPECT_LT(mismatchRate, 0.05f)
		<< "Mismatch rate: " << (mismatchRate * 100.0f) << "%"
		<< ", maxDiff: " << maxDiff
		<< ", mismatched: " << mismatched << "/" << totalPixels;
}

// ===========================================================================
// 2. Save cubemap faces as HDR files
// ===========================================================================

TEST_F(IBLConversionTest, SaveCubemapFacesAsHDR_ProducesValidFiles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Generate and convert
	auto srcPixels = GenerateEquirectGradient(kEquiWidth, kEquiHeight);
	auto equiImg = createAndUploadEquirect(srcPixels, kEquiWidth, kEquiHeight,
	                                        "IBLTest_HDR_Src");
	runE2C(*equiImg, *m_cubemapImage);

	// Read back cubemap faces
	auto cubeData = readbackCubemapFaces(*m_cubemapImage);
	const size_t faceFloats = static_cast<size_t>(kCubeFaceRes) * kCubeFaceRes * 4;

	static const char* kFaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

	const std::string outDir = kReferenceDir;
	ensureDir(outDir + "faces.hdr/.");

	for (int face = 0; face < 6; ++face)
	{
		const std::string path = outDir + "faces.hdr/cube_face_" + kFaceNames[face] + ".hdr";
		const float* faceData = cubeData.data() + face * faceFloats;
		bool saved = ImageData::SavePixelDataHDR(faceData, kCubeFaceRes, kCubeFaceRes, path);
		EXPECT_TRUE(saved) << "Failed to save HDR face " << kFaceNames[face];

		if (saved)
		{
			std::ifstream file(path, std::ios::binary);
			EXPECT_TRUE(file.is_open()) << "HDR file should exist: " << path;
			if (file.is_open())
			{
				std::string header(11, '\0');
				file.read(&header[0], 11);
				EXPECT_EQ(header, "#?RADIANCE\n") << "HDR file missing Radiance header: " << path;
			}
		}
	}
}

// ===========================================================================
// 3. Save HDR float image directly (CPU-side only, no GPU)
// ===========================================================================

TEST_F(IBLConversionTest, SaveHDRFloatImage_ProducesValidHDRFile)
{
	auto pixels = GenerateEquirectGradient(64, 32);

	const std::string outDir = kReferenceDir;
	ensureDir(outDir + ".");

	const std::string hdrPath = outDir + "test_gradient.hdr";
	bool saved = ImageData::SavePixelDataHDR(pixels.data(), 64, 32, hdrPath);
	EXPECT_TRUE(saved) << "Failed to save HDR file";

	if (saved)
	{
		std::ifstream file(hdrPath, std::ios::binary);
		EXPECT_TRUE(file.is_open()) << "HDR file should exist";

		// Verify header
		std::string header(11, '\0');
		file.read(&header[0], 11);
		EXPECT_EQ(header, "#?RADIANCE\n");

		// Verify file is not empty
		file.seekg(0, std::ios::end);
		const auto fileSize = file.tellg();
		EXPECT_GT(fileSize, 100) << "HDR file should contain pixel data";
	}
}

// ===========================================================================
// 4. Save cubemap faces as PNG (LDR)
// ===========================================================================

TEST_F(IBLConversionTest, SaveCubemapFacesAsPNG_ProducesValidFiles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Generate and convert
	auto srcPixels = GenerateEquirectGradient(kEquiWidth, kEquiHeight);
	auto equiImg = createAndUploadEquirect(srcPixels, kEquiWidth, kEquiHeight,
	                                        "IBLTest_PNG_Src");
	runE2C(*equiImg, *m_cubemapImage);

	// Read back cubemap faces as RGBA32F
	auto cubeData = readbackCubemapFaces(*m_cubemapImage);
	const auto* halfData = reinterpret_cast<const uint16_t*>(cubeData.data());
	const size_t facePixelCount = static_cast<size_t>(kCubeFaceRes) * kCubeFaceRes;

	static const char* kFaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

	const std::string outDir = kReferenceDir;
	ensureDir(outDir + "faces.png/.");

	for (int face = 0; face < 6; ++face)
	{
		const std::string path = outDir + "faces.png/cube_face_" + kFaceNames[face] + ".png";

		const uint16_t* faceSrc = halfData + face * facePixelCount * 4;
		auto u8Data = ImageData::ConvertHalfToU8(faceSrc, kCubeFaceRes, kCubeFaceRes, false);

		bool saved = ImageData::SavePixelData(u8Data.data(),
		                                       vk::Format::eR8G8B8A8Unorm,
		                                       vk::Extent2D{kCubeFaceRes, kCubeFaceRes},
		                                       path);
		EXPECT_TRUE(saved) << "Failed to save PNG face " << kFaceNames[face];
	}
}
