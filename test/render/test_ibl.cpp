/**
 * @file test_ibl.cpp
 * @brief Tests for IBL cubemap↔equirectangular conversion and HDR/LDR saving.
 *
 * Validates:
 *   - E2C (equirect → cubemap) compute shader produces valid cubemap
 *   - C2E (cubemap → equirect) compute shader roundtrip matches input
 *   - Cubemap 6-face readback and .hdr saving (Radiance format)
 *   - Cubemap 6-face .png saving (LDR)
 *   - Direct HDR float image saving via ImageData::SaveHDR
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "shared/TestReferenceImage.h"

#include "asset/ImageData.h"
#include "render/Barrier.h"
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
		m_e2cSetLayout = std::make_unique<DescriptorSetLayout>(
			BuildLayout()
				.AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
				            vk::ShaderStageFlagBits::eCompute)
				.AddBinding(1, vk::DescriptorType::eStorageImage,
				            vk::ShaderStageFlagBits::eCompute)
				.Build(dev));

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
		m_c2eSetLayout = std::make_unique<DescriptorSetLayout>(
			BuildLayout()
				.AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
				            vk::ShaderStageFlagBits::eCompute)
				.AddBinding(1, vk::DescriptorType::eStorageImage,
				            vk::ShaderStageFlagBits::eCompute)
				.Build(dev));

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
	// Image upload helper (delegates to Image::FromImageData)
	// -------------------------------------------------------------------

	std::unique_ptr<Image> createAndUploadEquirect(const std::vector<float>& pixelData,
	                                                uint32_t width,
	                                                uint32_t height,
	                                                const char* debugName)
	{
		ImageData imgData(pixelData.data(), width, height, vk::Format::eR32G32B32A32Sfloat);
		return Image::FromImageData(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily,
		                            imgData, debugName, vk::ImageUsageFlagBits::eStorage);
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

		// Transition cubemap: UNDEFINED → ShaderWrite (all 6 faces)
		vk::ImageSubresourceRange cubeRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6);
		Barrier::Transition(*cmd, cubeDst, ImageState::ShaderWrite, cubeRange);

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

		// Transition output equirect: UNDEFINED → ShaderWrite
		Barrier::Transition(*cmd, equiDst, ImageState::ShaderWrite);

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
	// Utility
	// -------------------------------------------------------------------

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
	auto resultData = m_outputEquirect->ReadImageData(
		*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);
	const float* resultPixels = reinterpret_cast<const float*>(resultData.GetPixelData().data());

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

	// Read back cubemap faces (all 6 layers)
	vk::ImageSubresourceRange cubeAll(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6);
	auto cubeData = m_cubemapImage->ReadImageData(
		*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily, &cubeAll);
	const float* cubeFloats = reinterpret_cast<const float*>(cubeData.GetPixelData().data());
	const size_t faceFloats = static_cast<size_t>(kCubeFaceRes) * kCubeFaceRes * 4;

	static const char* kFaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

	const std::string outDir = neurus::test::ReferencePath::Make("ibl/");
	std::filesystem::create_directories(std::filesystem::path(outDir + "faces.hdr"));

	for (int face = 0; face < 6; ++face)
	{
		const std::string path = outDir + "faces.hdr/cube_face_" + kFaceNames[face] + ".hdr";
		const float* faceData = cubeFloats + face * faceFloats;
		ImageData faceImg(faceData, kCubeFaceRes, kCubeFaceRes, vk::Format::eR32G32B32A32Sfloat);
		bool saved = faceImg.SaveHDR(path);
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

	const std::string hdrPath = neurus::test::ReferencePath::Make("ibl/test_gradient.hdr");
	std::filesystem::create_directories(std::filesystem::path(hdrPath).parent_path());

	ImageData gradientImg(pixels.data(), 64, 32, vk::Format::eR32G32B32A32Sfloat);
	bool saved = gradientImg.SaveHDR(hdrPath);
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
	vk::ImageSubresourceRange cubeAll(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6);
	auto cubeData = m_cubemapImage->ReadImageData(
		*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily, &cubeAll);
	const auto* halfData = reinterpret_cast<const uint16_t*>(cubeData.GetPixelData().data());
	const size_t facePixelCount = static_cast<size_t>(kCubeFaceRes) * kCubeFaceRes;

	static const char* kFaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

	const std::string outDir = neurus::test::ReferencePath::Make("ibl/");
	std::filesystem::create_directories(std::filesystem::path(outDir + "faces.png"));

	for (int face = 0; face < 6; ++face)
	{
		const std::string path = outDir + "faces.png/cube_face_" + kFaceNames[face] + ".png";

		const uint16_t* faceSrc = halfData + face * facePixelCount * 4;
		auto u8Data = ImageData::ConvertHalfToU8(faceSrc, kCubeFaceRes, kCubeFaceRes, false);

		ImageData img(u8Data.data(), kCubeFaceRes, kCubeFaceRes, vk::Format::eR8G8B8A8Unorm);
		bool saved = img.SavePNG(path);
		EXPECT_TRUE(saved) << "Failed to save PNG face " << kFaceNames[face];
	}
}
