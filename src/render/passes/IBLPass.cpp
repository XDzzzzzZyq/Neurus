/**
 * @file IBLPass.cpp
 * @brief Image-Based Lighting generation pass implementation.
 */

#include "passes/IBLPass.h"

#include "ComputePipelineBuilder.h"
#include "Image.h"
#include "asset/ImageData.h"
#include "shaders/ShaderModule.h"
#include "passes/SyncObjects.h"

#include "Log.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Push constant struct (shared by both irradiance and specular shaders)
// ---------------------------------------------------------------------------

/** @brief Push constants for IBL convolution compute shaders (16 bytes). */
struct alignas(16) IBLPushConstants
{
	int32_t mipLevel;      ///< Target mip level (unused by irradiance)
	int32_t maxStep;       ///< Diamond-pattern hemisphere step count
	float   roughnessSq;   ///< Roughness² (unused by irradiance)
	float   _pad;          ///< Padding to 16 bytes
};
static_assert(sizeof(IBLPushConstants) == 16, "IBLPushConstants must be 16 bytes");

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

IBLPass::IBLPass(const vk::raii::Device& device,
                 const vk::raii::PhysicalDevice& physicalDevice,
                 vk::Queue graphicsQueue,
                 uint32_t queueFamilyIndex,
                 const uint32_t* irradianceSpv,
                 size_t irradianceSize,
                 const uint32_t* specularSpv,
                 size_t specularSize)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
	, m_graphicsQueue(graphicsQueue)
	, m_queueFamilyIndex(queueFamilyIndex)
	// --- Samplers ---
	, m_equirectSampler(CreateEquirectSampler(device))
	, m_diffuseSampler(CreateCubemapSampler(device, 1))
	, m_specularSampler(CreateCubemapSampler(device, kSpecularMipLevels))
	// --- Descriptor set layout ---
	, m_descriptorSetLayout(CreateDescriptorSetLayout(device))
	// --- Descriptor pool ---
	, m_descriptorPool(device, 1,  // need 1 set
	                   DescriptorPool::CalculatePoolSizes({&m_descriptorSetLayout}, 1))
	// --- Allocate descriptor set ---
	, m_descriptorSets(m_descriptorPool.Allocate(m_descriptorSetLayout, 1))
	// --- Cubemaps (created in body) ---
	, m_diffuseCubemap(nullptr)
	, m_specularCubemap(nullptr)
	// --- Pipeline builders (must outlive pipelines) ---
	, m_irradiancePipelineBuilder(std::make_unique<ComputePipelineBuilder>(device))
	, m_irradiancePipeline(CreatePipeline(device, irradianceSpv, irradianceSize,
	                                       m_irradiancePipelineBuilder))
	, m_specularPipelineBuilder(std::make_unique<ComputePipelineBuilder>(device))
	, m_specularPipeline(CreatePipeline(device, specularSpv, specularSize,
	                                     m_specularPipelineBuilder))
{
	NEURUS_LOG("[IBLPass] irradianceSize=" << irradianceSize
	           << " specularSize=" << specularSize
	           << " qfi=" << queueFamilyIndex);

	// Create cubemap Images
	createCubemaps();
}

IBLPass::~IBLPass() = default;

// ---------------------------------------------------------------------------
// Cubemap creation
// ---------------------------------------------------------------------------

void IBLPass::createCubemaps()
{
	const vk::ImageUsageFlags cubeUsage =
		vk::ImageUsageFlagBits::eStorage      // compute write
		| vk::ImageUsageFlagBits::eSampled    // shader read
		| vk::ImageUsageFlagBits::eTransferSrc; // readback for saving

	// --- Diffuse irradiance cubemap (64², 1 mip) ---
	m_diffuseCubemap = std::make_unique<Image>(
		*m_device, *m_physicalDevice,
		vk::Extent2D{kDiffuseFaceRes, kDiffuseFaceRes},
		vk::Format::eR32G32B32A32Sfloat,
		cubeUsage,
		/*mipLevels=*/1,
		Image::ImageType::eCube,
		"IBL_DiffuseCubemap");

	// --- Specular prefiltered cubemap (2048², 8 mips) ---
	m_specularCubemap = std::make_unique<Image>(
		*m_device, *m_physicalDevice,
		vk::Extent2D{kSpecularFaceRes, kSpecularFaceRes},
		vk::Format::eR32G32B32A32Sfloat,
		cubeUsage,
		/*mipLevels=*/kSpecularMipLevels,
		Image::ImageType::eCube,
		"IBL_SpecularCubemap");

	// --- Create per-mip ImageViews for specular cubemap ---
	m_specularMipViews.reserve(kSpecularMipLevels);
	for (uint32_t mip = 0; mip < kSpecularMipLevels; ++mip)
	{
		const vk::ImageSubresourceRange subresourceRange(
			vk::ImageAspectFlagBits::eColor,
			mip,     // baseMipLevel
			1,       // levelCount
			0,       // baseArrayLayer
			6);      // layerCount (6 faces)

		const vk::ImageViewCreateInfo viewCI(
			{},
			*m_specularCubemap->ImageHandle(),
			vk::ImageViewType::eCube,
			vk::Format::eR32G32B32A32Sfloat,
			vk::ComponentMapping(),
			subresourceRange);

		m_specularMipViews.push_back(vk::raii::ImageView(*m_device, viewCI));
	}

	NEURUS_LOG("[IBLPass] Created diffuse cubemap " << kDiffuseFaceRes
	           << "², specular cubemap " << kSpecularFaceRes
	           << "² x " << kSpecularMipLevels << " mips");
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

void IBLPass::Generate(const Image& equirectImage)
{
	NEURUS_LOG("[IBLPass] Generating IBL cubemaps from equirect "
	           << equirectImage.Extent().width << "x" << equirectImage.Extent().height);

	// --- Create transient command pool ---
	const vk::CommandPoolCreateInfo poolCI(
		vk::CommandPoolCreateFlagBits::eTransient, m_queueFamilyIndex);
	vk::raii::CommandPool cmdPool(*m_device, poolCI);

	const vk::CommandBufferAllocateInfo allocInfo(
		*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(*m_device, allocInfo);

	// ===================================================================
	// Pass 1: Diffuse irradiance convolution (1 dispatch)
	// ===================================================================
	{
		vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
		cmdBufs[0].begin(beginInfo);

		// Transition equirect to SHADER_READ_ONLY if needed
		const auto eqLayout = equirectImage.CurrentLayout();
		if (eqLayout != vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			const auto barrier = ImageBarrier(
				*equirectImage.ImageHandle(),
				eqLayout,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits2::eAllCommands,
				vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderRead);
			const vk::DependencyInfo depInfo({}, {}, {}, barrier);
			cmdBufs[0].pipelineBarrier2(depInfo);
		}

		// Transition diffuse cubemap to GENERAL for compute write
		{
			const auto barrier = ImageBarrier(
				*m_diffuseCubemap->ImageHandle(),
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral,
				vk::PipelineStageFlagBits2::eTopOfPipe,
				vk::AccessFlagBits2::eNone,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderWrite);
			const vk::DependencyInfo depInfo({}, {}, {}, barrier);
			cmdBufs[0].pipelineBarrier2(depInfo);
			m_diffuseCubemap->SetCurrentLayout(vk::ImageLayout::eGeneral);
		}

		// Write descriptors with diffuse cubemap as output
		// Pass kSpecularMipLevels as sentinel to indicate "not specular"
		WriteDescriptors(equirectImage, kSpecularMipLevels);

		// Dispatch irradiance
		const uint32_t groupsX = (kDiffuseFaceRes + 3) / 4;
		const uint32_t groupsY = (kDiffuseFaceRes + 3) / 4;
		const uint32_t groupsZ = 6;  // 6 faces

		dispatchCompute(cmdBufs[0],
		                m_irradiancePipeline,
		                *m_irradiancePipelineBuilder->pipelineLayout(),
		                groupsX, groupsY, groupsZ,
		                /*mipLevel=*/0,
		                kDefaultIrradianceSteps,
		                /*roughnessSq=*/0.0f);

		// Memory barrier: make diffuse cubemap visible
		{
			const auto barrier = ImageBarrier(
				*m_diffuseCubemap->ImageHandle(),
				vk::ImageLayout::eGeneral,
				vk::ImageLayout::eGeneral,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderWrite,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderRead);
			const vk::DependencyInfo depInfo({}, {}, {}, barrier);
			cmdBufs[0].pipelineBarrier2(depInfo);
		}

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		m_graphicsQueue.submit(submitInfo);
		m_graphicsQueue.waitIdle();

		NEURUS_LOG("[IBLPass] Diffuse irradiance convolution complete");
	}

	// ===================================================================
	// Pass 2: Specular prefilter (8 dispatches, one per mip)
	// ===================================================================
	for (uint32_t mip = 0; mip < kSpecularMipLevels; ++mip)
	{
		const float roughness = static_cast<float>(mip) / static_cast<float>(kSpecularMipLevels - 1);
		const float roughnessSq = roughness * roughness;

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		// Transition specular cubemap at this mip to GENERAL
		// Use eUndefined for ALL mips (first use per mip; discarding is OK
		// since each mip is written independently).
		{
			const auto barrier = vk::ImageMemoryBarrier2(
				(mip == 0) ? vk::PipelineStageFlagBits2::eTopOfPipe
				           : vk::PipelineStageFlagBits2::eComputeShader,
				(mip == 0) ? vk::AccessFlagBits2::eNone
				           : vk::AccessFlagBits2::eShaderWrite,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderWrite,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				*m_specularCubemap->ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          mip, 1, 0, 6));
			const vk::DependencyInfo depInfo({}, {}, {}, barrier);
			cmdBufs[0].pipelineBarrier2(depInfo);
		}

		// Write descriptors using per-mip specular view
		WriteDescriptors(equirectImage, mip);

		// Compute dispatch dimensions for this mip
		const uint32_t faceRes = kSpecularFaceRes >> mip;
		const uint32_t groupsX = (faceRes + 3) / 4;
		const uint32_t groupsY = (faceRes + 3) / 4;
		const uint32_t groupsZ = 6;

		dispatchCompute(cmdBufs[0],
		                m_specularPipeline,
		                *m_specularPipelineBuilder->pipelineLayout(),
		                groupsX, groupsY, groupsZ,
		                static_cast<int32_t>(mip),
		                kDefaultSpecularSteps,
		                roughnessSq);

		// Memory barrier
		{
			const auto barrier = vk::ImageMemoryBarrier2(
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderWrite,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderRead,
				vk::ImageLayout::eGeneral,
				vk::ImageLayout::eGeneral,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				*m_specularCubemap->ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          mip, 1, 0, 6));
			const vk::DependencyInfo depInfo({}, {}, {}, barrier);
			cmdBufs[0].pipelineBarrier2(depInfo);
		}

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		m_graphicsQueue.submit(submitInfo);
		m_graphicsQueue.waitIdle();
	}

	// --- Final: transition cubemaps to SHADER_READ_ONLY_OPTIMAL ---
	{
		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		// Diffuse cubemap: GENERAL → SHADER_READ_ONLY
		{
			const auto barrier = ImageBarrier(
				*m_diffuseCubemap->ImageHandle(),
				vk::ImageLayout::eGeneral,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderWrite,
				vk::PipelineStageFlagBits2::eAllCommands,
				vk::AccessFlagBits2::eShaderRead);
			const vk::DependencyInfo depInfo({}, {}, {}, barrier);
			cmdBufs[0].pipelineBarrier2(depInfo);
			m_diffuseCubemap->SetCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
		}

		// Specular cubemap (all mips): GENERAL → SHADER_READ_ONLY
		{
			const auto barrier = ImageBarrier(
				*m_specularCubemap->ImageHandle(),
				vk::ImageLayout::eGeneral,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits2::eComputeShader,
				vk::AccessFlagBits2::eShaderWrite,
				vk::PipelineStageFlagBits2::eAllCommands,
				vk::AccessFlagBits2::eShaderRead,
				vk::ImageAspectFlagBits::eColor,
				/*baseMip=*/0,
				/*levelCount=*/kSpecularMipLevels);
			const vk::DependencyInfo depInfo({}, {}, {}, barrier);
			cmdBufs[0].pipelineBarrier2(depInfo);
			m_specularCubemap->SetCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
		}

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		m_graphicsQueue.submit(submitInfo);
		m_graphicsQueue.waitIdle();
	}

	NEURUS_LOG("[IBLPass] IBL generation complete – diffuse "
	           << kDiffuseFaceRes << "², specular "
	           << kSpecularFaceRes << "² x " << kSpecularMipLevels << " mips");
}

// ---------------------------------------------------------------------------
// Descriptor set layout
// ---------------------------------------------------------------------------

DescriptorSetLayout IBLPass::CreateDescriptorSetLayout(const vk::raii::Device& device)
{
	auto bindings = BuildLayout()
		// Equirect input (combined image sampler)
		.AddBinding(0,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// Cubemap output (storage image – image2DArray)
		.AddBinding(1,
		            vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		.Build();

	return DescriptorSetLayout(device, bindings);
}

// ---------------------------------------------------------------------------
// Sampler factories
// ---------------------------------------------------------------------------

vk::raii::Sampler IBLPass::CreateEquirectSampler(const vk::raii::Device& device)
{
	vk::SamplerCreateInfo samplerCI(
		{},
		vk::Filter::eLinear,
		vk::Filter::eLinear,
		vk::SamplerMipmapMode::eNearest,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		0.0f,
		VK_FALSE,
		0.0f,
		VK_FALSE,
		vk::CompareOp::eAlways,
		0.0f,
		0.0f,
		vk::BorderColor::eFloatTransparentBlack,
		VK_FALSE);

	return vk::raii::Sampler(device, samplerCI);
}

vk::raii::Sampler IBLPass::CreateCubemapSampler(const vk::raii::Device& device,
                                                  uint32_t mipLevels)
{
	vk::SamplerCreateInfo samplerCI(
		{},
		vk::Filter::eLinear,
		vk::Filter::eLinear,
		vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		0.0f,
		VK_FALSE,
		0.0f,
		VK_FALSE,
		vk::CompareOp::eAlways,
		0.0f,
		static_cast<float>(mipLevels),
		vk::BorderColor::eFloatTransparentBlack,
		VK_FALSE);

	return vk::raii::Sampler(device, samplerCI);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

vk::raii::Pipeline IBLPass::CreatePipeline(const vk::raii::Device& device,
                                            const uint32_t* compSpv,
                                            size_t compSize,
                                            std::unique_ptr<ComputePipelineBuilder>& outBuilder)
{
	auto compModule = ShaderModule::FromEmbedded(device, compSpv, compSize);

	outBuilder = std::make_unique<ComputePipelineBuilder>(device);

	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		sizeof(IBLPushConstants));

	return outBuilder->SetShaderStage(compModule, "main")
		.AddDescriptorSetLayout(*m_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void IBLPass::WriteDescriptors(const Image& equirectImage, uint32_t specularMip)
{
	auto& dstSet = m_descriptorSets[0];

	// --- Binding 0: equirect input (combined image sampler) ---
	{
		vk::DescriptorImageInfo imageInfo(
			*m_equirectSampler,
			*equirectImage.ImageViewHandle(),
			vk::ImageLayout::eShaderReadOnlyOptimal);

		dstSet.WriteImage(0, imageInfo,
		                   vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Binding 1: cubemap output (storage image) ---
	// For diffuse: use the diffuse cubemap view (mip 0, all layers)
	// For specular: use the per-mip view for the current mip level
	{
		const bool isSpecular = (specularMip < kSpecularMipLevels);

		vk::DescriptorImageInfo imageInfo(
			nullptr,
			isSpecular ? *m_specularMipViews[specularMip]
			           : *m_diffuseCubemap->ImageViewHandle(),
			vk::ImageLayout::eGeneral);

		dstSet.WriteImage(1, imageInfo,
		                   vk::DescriptorType::eStorageImage);
	}
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void IBLPass::dispatchCompute(vk::CommandBuffer cmdBuf,
                               const vk::raii::Pipeline& pipeline,
                               vk::PipelineLayout pipelineLayout,
                               uint32_t groupCountX,
                               uint32_t groupCountY,
                               uint32_t groupCountZ,
                               int32_t mipLevel,
                               int32_t maxStep,
                               float roughnessSq)
{
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);

	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          pipelineLayout,
	                          0,
	                          {m_descriptorSets[0].handle()},
	                          {});

	IBLPushConstants pc = {};
	pc.mipLevel    = mipLevel;
	pc.maxStep     = maxStep;
	pc.roughnessSq = roughnessSq;

	cmdBuf.pushConstants<IBLPushConstants>(
		pipelineLayout,
		vk::ShaderStageFlagBits::eCompute,
		0,
		pc);

	cmdBuf.dispatch(groupCountX, groupCountY, groupCountZ);
}

// ---------------------------------------------------------------------------
// Saving cubemaps (GPU readback → HDR)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Per-face cubemap readback helper
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Reads back all 6 faces of a cubemap into a flat float array.
 *
 * Creates a staging buffer, transitions the cubemap to TRANSFER_SRC_OPTIMAL,
 * copies all 6 layers in one vkCmdCopyImageToBuffer, submits and waits.
 * The cubemap is left in TRANSFER_SRC_OPTIMAL layout.
 *
 * @return Float pixel data (6 faces × faceRes² × 4 floats), or empty on failure.
 */
std::vector<float> readbackCubemapFaces(const vk::raii::Device& device,
                                         const vk::raii::PhysicalDevice& physicalDevice,
                                         vk::Queue queue,
                                         uint32_t queueFamilyIndex,
                                         Image& cubemap,
                                         uint32_t faceRes)
{
	const vk::DeviceSize bufSize = static_cast<vk::DeviceSize>(faceRes) * faceRes * 6 * 16; // 6 faces, R32G32B32A32_SFLOAT

	// --- Staging buffer ---
	vk::BufferCreateInfo stagingCI({}, bufSize, vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuf(device, stagingCI);

	auto memReqs = stagingBuf.getMemoryRequirements();
	uint32_t memType = 0;
	{
		auto memProps = physicalDevice.getMemoryProperties();
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((memReqs.memoryTypeBits & (1u << i)) &&
			    (memProps.memoryTypes[i].propertyFlags &
			     (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) ==
			        (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))
			{
				memType = i;
				break;
			}
		}
	}
	vk::raii::DeviceMemory stagingMem(device, vk::MemoryAllocateInfo(memReqs.size, memType));
	stagingBuf.bindMemory(*stagingMem, 0);

	// --- Transient command buffer ---
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient, queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, poolCI);
	vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocInfo);

	cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	// Transition UNDEFINED → TRANSFER_SRC_OPTIMAL if needed (handles final layouts)
	const auto srcLayout = cubemap.CurrentLayout();
	if (srcLayout != vk::ImageLayout::eTransferSrcOptimal)
	{
		const vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead,
			vk::AccessFlagBits::eTransferRead,
			srcLayout,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*cubemap.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, cubemap.MipLevels(), 0, 6));
		cmdBufs[0].pipelineBarrier(
			vk::PipelineStageFlagBits::eComputeShader,
			vk::PipelineStageFlagBits::eTransfer,
			{}, {}, {}, {barrier});
	}

	// Copy all 6 faces (mip 0 only)
	vk::BufferImageCopy copyRegion(
		0, 0, 0,
		vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 6),
		vk::Offset3D(0, 0, 0),
		vk::Extent3D(faceRes, faceRes, 1));
	cmdBufs[0].copyImageToBuffer(*cubemap.ImageHandle(),
	                              vk::ImageLayout::eTransferSrcOptimal,
	                              *stagingBuf, {copyRegion});

	vk::MemoryBarrier memBarrier(vk::AccessFlagBits::eTransferWrite,
	                             vk::AccessFlagBits::eHostRead);
	cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                           vk::PipelineStageFlagBits::eHost,
	                           {}, {memBarrier}, {}, {});

	cmdBufs[0].end();

	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
	queue.submit(submitInfo);
	queue.waitIdle();

	std::vector<float> result(static_cast<size_t>(faceRes) * faceRes * 6 * 4);
	void* mapped = stagingMem.mapMemory(0, bufSize);
	std::memcpy(result.data(), mapped, static_cast<size_t>(bufSize));
	stagingMem.unmapMemory();

	return result;
}

} // anonymous namespace

bool IBLPass::SaveDiffuseCubemap(const std::string& pathPrefix)
{
	if (!m_diffuseCubemap)
	{
		return false;
	}

	const auto faceRes = kDiffuseFaceRes;
	auto cubeData = readbackCubemapFaces(*m_device, *m_physicalDevice,
	                                      m_graphicsQueue, m_queueFamilyIndex,
	                                      *m_diffuseCubemap, faceRes);
	if (cubeData.empty())
	{
		NEURUS_ERR("[IBLPass] SaveDiffuseCubemap: readback failed");
		return false;
	}

	const size_t faceFloats = static_cast<size_t>(faceRes) * faceRes * 4;
	static const char* kFaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

	bool allSaved = true;
	for (int face = 0; face < 6; ++face)
	{
		const std::string path = pathPrefix + "_diffuse_" + kFaceNames[face] + ".hdr";
		const float* faceData = cubeData.data() + face * faceFloats;
		const bool saved = ImageData::SavePixelDataHDR(faceData, faceRes, faceRes, path);
		if (saved)
		{
			NEURUS_LOG("[IBLPass] Saved diffuse cubemap face: " << path);
		}
		else
		{
			NEURUS_ERR("[IBLPass] Failed to save diffuse face: " << path);
			allSaved = false;
		}
	}

	return allSaved;
}

bool IBLPass::SaveSpecularCubemap(const std::string& pathPrefix)
{
	if (!m_specularCubemap)
	{
		return false;
	}

	// Read mip 0 only (highest detail)
	const auto faceRes = kSpecularFaceRes;
	auto cubeData = readbackCubemapFaces(*m_device, *m_physicalDevice,
	                                      m_graphicsQueue, m_queueFamilyIndex,
	                                      *m_specularCubemap, faceRes);
	if (cubeData.empty())
	{
		NEURUS_ERR("[IBLPass] SaveSpecularCubemap: readback failed");
		return false;
	}

	const size_t faceFloats = static_cast<size_t>(faceRes) * faceRes * 4;
	static const char* kFaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

	bool allSaved = true;
	for (int face = 0; face < 6; ++face)
	{
		const std::string path = pathPrefix + "_specular_" + kFaceNames[face] + ".hdr";
		const float* faceData = cubeData.data() + face * faceFloats;
		const bool saved = ImageData::SavePixelDataHDR(faceData, faceRes, faceRes, path);
		if (saved)
		{
			NEURUS_LOG("[IBLPass] Saved specular cubemap face: " << path);
		}
		else
		{
			NEURUS_ERR("[IBLPass] Failed to save specular face: " << path);
			allSaved = false;
		}
	}

	return allSaved;
}

} // namespace neurus
