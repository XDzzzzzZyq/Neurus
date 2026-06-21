/**
 * @file LightingPass.cpp
 * @brief PBR lighting pass implementation.
 */

#include "passes/LightingPass.h"

#include "passes/AttachmentManager.h"
#include "ComputePipelineBuilder.h"
#include "Image.h"
#include "shaders/ShaderModule.h"

#include "Log.h"

#include "scene/Light.h"
#include "scene/Scene.h"

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LightingPass::LightingPass(const vk::raii::Device& device,
                           const vk::raii::PhysicalDevice& physicalDevice,
                           AttachmentManager& attachmentManager,
                           uint32_t numSets,
                           vk::Queue graphicsQueue,
                           uint32_t queueFamilyIndex,
                           const uint32_t* compSpv,
                           size_t compSize)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
	, m_attachmentManager(&attachmentManager)
	, m_graphicsQueue(graphicsQueue)
	, m_queueFamilyIndex(queueFamilyIndex)
	// --- Nearest-neighbour sampler for G-Buffer reads ---
	, m_sampler(CreateSampler(device, physicalDevice))
	// --- Descriptor set layout ---
	, m_descriptorSetLayout(CreateDescriptorSetLayout(device))
	// --- Descriptor pool (numSets sets, pool sizes from layout × numSets) ---
	, m_descriptorPool(device,
	                   numSets,
	                   DescriptorPool::CalculatePoolSizes({&m_descriptorSetLayout}, numSets))
	// --- Descriptor sets (one per in-flight frame) ---
	, m_descriptorSets(m_descriptorPool.Allocate(m_descriptorSetLayout, numSets))
	// --- Pipeline builder (must outlive pipeline) ---
	, m_pipelineBuilder(std::make_unique<ComputePipelineBuilder>(device))
	, m_pipeline(CreatePipeline(device, compSpv, compSize))
{
	NEURUS_LOG("[LightingPass] compSize=" << compSize << " numSets=" << numSets
	           << " qfi=" << queueFamilyIndex);

	// --- Create fallback SSBO for zero-light scenes ---
	//     The SSBO descriptor (binding 5) must always reference a valid buffer,
	//     even when the scene has no point lights. This fallback is a single
	//     zero-filled element that the shader never reads (lightCount=0).
	{
		uint8_t zero[sizeof(PointLightGpu)] = {};
		m_fallbackSSBO = std::make_unique<VulkanBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			sizeof(PointLightGpu),
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			"FallbackLightSSBO");
		m_fallbackSSBO->Upload(zero, sizeof(PointLightGpu));
		NEURUS_LOG("[LightingPass] Created fallback SSBO (zero-light)");
	}

	// --- Create fallback IBL cubemaps (4×4 black) for bindings 7-8 ---
	//     These ensure the descriptor bindings are always valid even when
	//     SetIBLResources() has not been called (no HDR loaded).
	//     Using 4×4 faces to satisfy minimum cubemap dimension requirements.
	{
		const vk::ImageUsageFlags cubeUsage =
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		const vk::Extent2D fbExtent{4, 4};

		// --- Fallback diffuse irradiance cubemap ---
		m_fallbackIrradianceCube = new Image(
			device, physicalDevice, fbExtent,
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage, /*mipLevels=*/1,
			Image::ImageType::eCube,
			"Lighting_IrradianceFallback");

		// --- Fallback specular prefiltered cubemap ---
		m_fallbackPrefilteredCube = new Image(
			device, physicalDevice, fbExtent,
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage, /*mipLevels=*/1,
			Image::ImageType::eCube,
			"Lighting_PrefilteredFallback");

		// Transition fallback cubemaps from UNDEFINED to SHADER_READ_ONLY_OPTIMAL
		// so that the descriptor image layout matches the actual image layout.
		{
			vk::CommandPoolCreateInfo poolCI(
				vk::CommandPoolCreateFlagBits::eTransient,
				m_queueFamilyIndex);
			vk::raii::CommandPool cmdPool(*m_device, poolCI);

			vk::CommandBufferAllocateInfo allocInfo(
				*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
			vk::raii::CommandBuffers cmdBufs(*m_device, allocInfo);

			cmdBufs[0].begin(vk::CommandBufferBeginInfo(
				vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			m_fallbackIrradianceCube->TransitionLayout(
				cmdBufs[0],
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				0, VK_REMAINING_MIP_LEVELS,
				0, VK_REMAINING_ARRAY_LAYERS);

			m_fallbackPrefilteredCube->TransitionLayout(
				cmdBufs[0],
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				0, VK_REMAINING_MIP_LEVELS,
				0, VK_REMAINING_ARRAY_LAYERS);

			cmdBufs[0].end();

			vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
			m_graphicsQueue.submit(submitInfo);
			m_graphicsQueue.waitIdle();
		}

		// Sampler for fallback cubemaps (maxLod=0 - only mip 0 exists)
		{
			vk::SamplerCreateInfo samplerCI(
				{}, vk::Filter::eNearest, vk::Filter::eNearest,
				vk::SamplerMipmapMode::eNearest,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				0.0f, VK_FALSE, 0.0f, VK_FALSE,
				vk::CompareOp::eAlways, 0.0f, 0.0f,  // minLod=0, maxLod=0
				vk::BorderColor::eFloatTransparentBlack, VK_FALSE);
			m_fallbackCubeSampler = vk::raii::Sampler(device, samplerCI);
		}

		NEURUS_LOG("[LightingPass] Created fallback IBL cubemaps (4×4 black)");
	}

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets; ++i)
	{
		const std::string dsName = "LightingPass_Set" + std::to_string(i);
		m_descriptorSets[i].SetDebugName(dsName.c_str());
	}
#endif
}

LightingPass::~LightingPass()
{
	delete m_fallbackIrradianceCube;
	delete m_fallbackPrefilteredCube;
}

// ---------------------------------------------------------------------------
// Light SSBO management
// ---------------------------------------------------------------------------

void LightingPass::UploadLights(const Scene& scene)
{
	// Collect only point lights
	std::vector<PointLightGpu> gpuLights;
	gpuLights.reserve(scene.light_list.size());

	for (const auto& [id, light] : scene.light_list)
	{
		if (light->light_type != LightType::POINTLIGHT)
		{
			continue;
		}

		PointLightGpu gpu = {};
		const auto& pos = light->GetPosition();

		// Position (world-space, from Transform3D)
		gpu.posX = pos.x;
		gpu.posY = pos.y;
		gpu.posZ = pos.z;

		// Color (linear RGB)
		gpu.colorR = light->light_color.r;
		gpu.colorG = light->light_color.g;
		gpu.colorB = light->light_color.b;

		// Lighting parameters
		gpu.power = light->light_power;
		gpu.radius = light->light_radius;

		gpuLights.push_back(gpu);
	}

	const uint32_t newCount = static_cast<uint32_t>(gpuLights.size());
	m_lightCount = newCount;

	if (newCount == 0)
	{
		m_lightSSBO.reset();
		NEURUS_LOG("[LightingPass] No point lights in scene - SSBO released (fallback preserved)");
		return;
	}

	// Create or re-create the SSBO
	const vk::DeviceSize bufferSize = newCount * sizeof(PointLightGpu);

	m_lightSSBO = std::make_unique<VulkanBuffer>(
		*m_device, *m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex,
		bufferSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal,
		"LightSSBO");
	m_lightSSBO->Upload(gpuLights.data(), bufferSize);

	NEURUS_LOG("[LightingPass] Uploaded " << newCount << " point lights"
	           << " (" << bufferSize << " bytes)");
}

const VulkanBuffer* LightingPass::GetLightSSBO() const
{
	return m_lightSSBO ? m_lightSSBO.get() : m_fallbackSSBO.get();
}

uint32_t LightingPass::GetLightCount() const
{
	return m_lightCount;
}

// ---------------------------------------------------------------------------
// Descriptor set layout
// ---------------------------------------------------------------------------

DescriptorSetLayout LightingPass::CreateDescriptorSetLayout(const vk::raii::Device& device)
{
	auto bindings = BuildLayout()
		// G-Buffer inputs (combined image samplers)
		.AddBinding(0,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(2,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(3,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// Output HDR colour (storage image)
		.AddBinding(4,
		            vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		// Light SSBO
		.AddBinding(5,
		            vk::DescriptorType::eStorageBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		// SSAO occlusion input (combined image sampler)
		.AddBinding(6,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// IBL diffuse irradiance cubemap (combined image sampler)
		.AddBinding(7,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// IBL specular prefiltered cubemap (combined image sampler)
		.AddBinding(8,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.Build();

	return DescriptorSetLayout(device, bindings);
}

// ---------------------------------------------------------------------------
// Sampler factory
// ---------------------------------------------------------------------------

vk::raii::Sampler LightingPass::CreateSampler(const vk::raii::Device& device,
                                               const vk::raii::PhysicalDevice& physicalDevice)
{
	vk::SamplerCreateInfo samplerCI(
		{},                                    // flags
		vk::Filter::eNearest,                  // magFilter
		vk::Filter::eNearest,                  // minFilter
		vk::SamplerMipmapMode::eNearest,        // mipmapMode
		vk::SamplerAddressMode::eClampToEdge,   // addressModeU
		vk::SamplerAddressMode::eClampToEdge,   // addressModeV
		vk::SamplerAddressMode::eClampToEdge,   // addressModeW
		0.0f,                                   // mipLodBias
		VK_FALSE,                               // anisotropyEnable
		0.0f,                                   // maxAnisotropy
		VK_FALSE,                               // compareEnable
		vk::CompareOp::eAlways,                 // compareOp
		0.0f,                                   // minLod
		0.0f,                                   // maxLod
		vk::BorderColor::eFloatTransparentBlack, // borderColor
		VK_FALSE                                // unnormalizedCoordinates
	);

	return vk::raii::Sampler(device, samplerCI);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

vk::raii::Pipeline LightingPass::CreatePipeline(const vk::raii::Device& device,
                                                 const uint32_t* compSpv,
                                                 size_t compSize)
{
	// --- Create compute shader module from embedded SPIR-V ---
	auto compModule = ShaderModule::FromEmbedded(device, compSpv, compSize);

	// --- Push constant range ---
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		sizeof(LightingPushConstants));  // 100 bytes

	// --- Build compute pipeline ---
	return m_pipelineBuilder->SetShaderStage(compModule, "main")
		.AddDescriptorSetLayout(*m_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// IBL resource injection
// ---------------------------------------------------------------------------

void LightingPass::SetIBLResources(vk::ImageView irradianceView,
                                    vk::Sampler irradianceSampler,
                                    vk::ImageView prefilteredView,
                                    vk::Sampler prefilteredSampler)
{
	m_iblIrradianceView    = irradianceView;
	m_iblIrradianceSampler  = irradianceSampler;
	m_iblPrefilteredView    = prefilteredView;
	m_iblPrefilteredSampler = prefilteredSampler;
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void LightingPass::WriteDescriptors(uint32_t setIndex)
{
	DescriptorSet& dstSet = m_descriptorSets[setIndex];

	const std::array<AttachmentName, 4> gBufferInputs = {
		AttachmentName::Position,
		AttachmentName::Normal,
		AttachmentName::Albedo,
		AttachmentName::MetallicRoughness,
	};

	// --- Write G-Buffer input descriptors (combined image samplers) ---
	for (uint32_t i = 0; i < 4; ++i)
	{
		const auto& attachment = m_attachmentManager->GetAttachment(gBufferInputs[i]);

		vk::DescriptorImageInfo imageInfo(
			*m_sampler,                              // sampler
			*attachment.ImageViewHandle(),           // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(i, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Write HDR colour output (storage image) ---
	{
		const auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);

		vk::DescriptorImageInfo imageInfo(
			nullptr,                              // sampler (not used for storage images)
			*hdrColor.ImageViewHandle(),          // imageView
			vk::ImageLayout::eGeneral             // imageLayout
		);

		dstSet.WriteImage(4, imageInfo,
		                  vk::DescriptorType::eStorageImage);
	}

	// --- Write light SSBO (uses fallback when no lights present) ---
	{
		dstSet.WriteBuffer(5, GetLightSSBO()->GetDescriptorInfo(),
		                   vk::DescriptorType::eStorageBuffer);
	}

	// --- Write SSAO attachment (combined image sampler) ---
	{
		const auto& ssao = m_attachmentManager->GetAttachment(AttachmentName::SSAO);

		vk::DescriptorImageInfo imageInfo(
			*m_sampler,                              // sampler
			*ssao.ImageViewHandle(),                 // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(6, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Write IBL irradiance cubemap (binding 7) ---
	{
		const bool hasIBL = (m_iblIrradianceView != vk::ImageView{});
		const vk::Sampler sampler = hasIBL ? m_iblIrradianceSampler : *m_fallbackCubeSampler;

		Image& fbIrrad = *m_fallbackIrradianceCube;
		const vk::ImageView fallbackView = *fbIrrad.ImageViewHandle();
		const vk::ImageView view = hasIBL ? m_iblIrradianceView : fallbackView;

		vk::DescriptorImageInfo imageInfo(sampler, view,
		                                  vk::ImageLayout::eShaderReadOnlyOptimal);

		dstSet.WriteImage(7, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Write IBL specular cubemap (binding 8) ---
	{
		const bool hasIBL = (m_iblPrefilteredView != vk::ImageView{});
		const vk::Sampler sampler = hasIBL ? m_iblPrefilteredSampler : *m_fallbackCubeSampler;

		Image& fbSpec = *m_fallbackPrefilteredCube;
		const vk::ImageView fallbackView = *fbSpec.ImageViewHandle();
		const vk::ImageView view = hasIBL ? m_iblPrefilteredView : fallbackView;

		vk::DescriptorImageInfo imageInfo(sampler, view,
		                                  vk::ImageLayout::eShaderReadOnlyOptimal);

		dstSet.WriteImage(8, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void LightingPass::Record(vk::CommandBuffer cmdBuf,
                          const glm::vec3& cameraPos,
                          const glm::mat4& viewMatrix,
                          const glm::mat4& invProjView,
                          vk::Extent2D renderExtent,
                          uint32_t frameIndex)
{
	// --- 1. Write descriptor set for this frame slot ---
	WriteDescriptors(frameIndex);

	// --- 2. Transition G-Buffer images to SHADER_READ_ONLY_OPTIMAL ---
	//     and HDRColor to GENERAL for compute write.
	//     Also transition SSAO from GENERAL to SHADER_READ_ONLY_OPTIMAL.
	//     Uses pipelineBarrier2 (synchronization2) for correct layout
	//     tracking with VK_KHR_dynamic_rendering.
	{
		const std::array<AttachmentName, 4> gBufferInputs = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,
			AttachmentName::MetallicRoughness,
		};

		std::array<vk::ImageMemoryBarrier2, 6> barriers;

		for (size_t i = 0; i < 4; ++i)
		{
			auto& attachment = m_attachmentManager->GetAttachment(gBufferInputs[i]);
			const vk::ImageLayout oldLayout = attachment.CurrentLayout();

			barriers[i] = vk::ImageMemoryBarrier2(
				(oldLayout == vk::ImageLayout::eColorAttachmentOptimal)
					? vk::PipelineStageFlagBits2::eColorAttachmentOutput
					: vk::PipelineStageFlagBits2::eComputeShader,         // srcStage
				(oldLayout == vk::ImageLayout::eColorAttachmentOptimal)
					? vk::AccessFlagBits2::eColorAttachmentWrite
					: vk::AccessFlagBits2::eShaderWrite,                  // srcAccess
				vk::PipelineStageFlagBits2::eComputeShader,               // dstStage
				vk::AccessFlagBits2::eShaderRead,                         // dstAccess
				oldLayout,                                                 // oldLayout (actual)
				vk::ImageLayout::eShaderReadOnlyOptimal,                  // newLayout
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				*attachment.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          0, 1, 0, 1));

			// Update CPU-side layout tracking
			attachment.SetCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
		}

		// HDRColor: current layout → GENERAL
		auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
		const vk::ImageLayout hdrOldLayout = hdrColor.CurrentLayout();
		barriers[4] = vk::ImageMemoryBarrier2(
			(hdrOldLayout == vk::ImageLayout::eUndefined)
				? vk::PipelineStageFlagBits2::eTopOfPipe
				: vk::PipelineStageFlagBits2::eAllCommands,            // srcStage
			(hdrOldLayout == vk::ImageLayout::eUndefined)
				? vk::AccessFlagBits2::eNone
				: vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,  // srcAccess
			vk::PipelineStageFlagBits2::eComputeShader,             // dstStage
			vk::AccessFlagBits2::eShaderWrite,                      // dstAccess
			hdrOldLayout,                                            // oldLayout - tracked, correct
			vk::ImageLayout::eGeneral,                              // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*hdrColor.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));

		// Update CPU-side layout tracking (the barrier did this implicitly on GPU)
		hdrColor.SetCurrentLayout(vk::ImageLayout::eGeneral);

		// SSAO: GENERAL → SHADER_READ_ONLY (was written by SSAOPass, now read by lighting)
		auto& ssao = m_attachmentManager->GetAttachment(AttachmentName::SSAO);
		vk::ImageLayout ssaoOldLayout = ssao.CurrentLayout();

		// If SSAO attachment hasn't been written (layout = UNDEFINED), clear to 1.0 (no occlusion)
		bool ssaoWasCleared = false;
		if (ssaoOldLayout == vk::ImageLayout::eUndefined)
		{
			const auto preClearBarrier = vk::ImageMemoryBarrier2(
				vk::PipelineStageFlagBits2::eTopOfPipe,
				vk::AccessFlagBits2::eNone,
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				*ssao.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          0, 1, 0, 1));

			const vk::DependencyInfo preDep({}, {}, {}, preClearBarrier);
			cmdBuf.pipelineBarrier2(preDep);

			vk::ClearColorValue clearWhite(std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
			cmdBuf.clearColorImage(*ssao.ImageHandle(),
			                       vk::ImageLayout::eTransferDstOptimal,
			                       clearWhite,
			                       vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                                                  0, 1, 0, 1));

			ssaoOldLayout = vk::ImageLayout::eTransferDstOptimal;
			ssao.SetCurrentLayout(vk::ImageLayout::eTransferDstOptimal);
			ssaoWasCleared = true;
		}

		const vk::PipelineStageFlags2 ssaoSrcStage = ssaoWasCleared
			? vk::PipelineStageFlagBits2::eTransfer
			: vk::PipelineStageFlagBits2::eComputeShader;
		const vk::AccessFlags2 ssaoSrcAccess = ssaoWasCleared
			? vk::AccessFlagBits2::eTransferWrite
			: vk::AccessFlagBits2::eShaderWrite;

		barriers[5] = vk::ImageMemoryBarrier2(
			ssaoSrcStage,                                               // srcStage
			ssaoSrcAccess,                                              // srcAccess
			vk::PipelineStageFlagBits2::eComputeShader,               // dstStage (lighting read)
			vk::AccessFlagBits2::eShaderRead,                          // dstAccess
			ssaoOldLayout,                                              // oldLayout
			vk::ImageLayout::eShaderReadOnlyOptimal,                   // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*ssao.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));

		ssao.SetCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

		const vk::DependencyInfo depInfo({}, {}, {}, barriers);
		cmdBuf.pipelineBarrier2(depInfo);
	}

	// --- 3. Bind compute pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);

	// --- 4. Bind descriptor set ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          *m_pipelineBuilder->pipelineLayout(),
	                          0,                                    // firstSet
	                          {m_descriptorSets[frameIndex].handle()},
	                          {});

	// --- 5. Push constants ---
	{
		LightingPushConstants pc = {};
		pc.lightCount = static_cast<int32_t>(m_lightCount);
		pc.camX = cameraPos.x;
		pc.camY = cameraPos.y;
		pc.camZ = cameraPos.z;
		// Copy view matrix (column-major, same as GLSL)
		const float* vm = &viewMatrix[0][0];
		for (int i = 0; i < 16; ++i)
		{
			pc.view[i] = vm[i];
		}

		// Enable IBL when cubemap resources are available
		pc.iblEnabled = (m_iblIrradianceView != vk::ImageView{}) ? 1 : 0;

		// Copy inverse(proj * view) matrix for skybox background ray
		const float* ipv = &invProjView[0][0];
		for (int i = 0; i < 16; ++i)
		{
			pc.invProjView[i] = ipv[i];
		}

		cmdBuf.pushConstants<LightingPushConstants>(
			*m_pipelineBuilder->pipelineLayout(),
			vk::ShaderStageFlagBits::eCompute,
			0,
			pc);
	}

	// --- 6. Dispatch ---
	const uint32_t groupCountX = (renderExtent.width  + 15) / 16;
	const uint32_t groupCountY = (renderExtent.height + 15) / 16;
	cmdBuf.dispatch(groupCountX, groupCountY, 1);

	// --- 7. Memory barrier: make HDR output visible for subsequent passes ---
	//     Uses pipelineBarrier2 for consistent layout tracking.
	{
		const auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
		const vk::ImageMemoryBarrier2 barrier(
			vk::PipelineStageFlagBits2::eComputeShader,          // srcStage
			vk::AccessFlagBits2::eShaderWrite,                    // srcAccess
			vk::PipelineStageFlagBits2::eComputeShader,            // dstStage
			vk::AccessFlagBits2::eShaderRead,                      // dstAccess
			vk::ImageLayout::eGeneral,                             // oldLayout
			vk::ImageLayout::eGeneral,                             // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*hdrColor.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));

		const vk::DependencyInfo depInfo({}, {}, {}, barrier);
		cmdBuf.pipelineBarrier2(depInfo);
	}
}

} // namespace neurus
