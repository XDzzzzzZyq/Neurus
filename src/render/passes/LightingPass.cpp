/**
 * @file LightingPass.cpp
 * @brief PBR lighting pass implementation.
 */

#include "passes/LightingPass.h"

#include "RenderCache.h"
#include "RenderContext.h"
#include "ComputePipelineBuilder.h"
#include "Image.h"
#include "render/Barrier.h"
#include "shaders/ShaderModule.h"
#include "Texture.h"

#include "Log.h"

#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Scene.h"

#include <array>
#include <cstdint>
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
                           uint32_t numSets,
                           vk::Queue graphicsQueue,
                           uint32_t queueFamilyIndex,
                           const uint32_t* compSpv,
                           size_t compSize)
	: ComputePass(device, physicalDevice,
	              LightingPass::CreateDescriptorSetLayout(device), numSets)
	, m_graphicsQueue(graphicsQueue)
	, m_queueFamilyIndex(queueFamilyIndex)
	, m_pipeline(CreatePipeline(device, compSpv, compSize))
{
	NEURUS_LOG("[LightingPass] compSize=" << compSize << " numSets=" << numSets
	           << " qfi=" << queueFamilyIndex);

	// --- Create fallback IBL cubemaps (4×4 black) for bindings 7-8 ---
	//     These ensure the descriptor bindings are always valid even when
	//     no Environment is present in the scene (no IBL to sample).
	//     Using 4×4 faces to satisfy minimum cubemap dimension requirements.
	{
		const vk::ImageUsageFlags cubeUsage =
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		const vk::Extent2D fbExtent{4, 4};

		// --- Fallback diffuse irradiance cubemap ---
		m_fallbackIrradianceCube = std::make_unique<Image>(
			*m_device, *m_physicalDevice, fbExtent,
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage, /*mipLevels=*/1,
			Image::ImageType::eCube,
			"Lighting_IrradianceFallback");

		// --- Fallback specular prefiltered cubemap ---
		m_fallbackPrefilteredCube = std::make_unique<Image>(
			*m_device, *m_physicalDevice, fbExtent,
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

			Barrier::Transition(*cmdBufs[0], *m_fallbackIrradianceCube, ImageState::ColorShaderRead);

			Barrier::Transition(*cmdBufs[0], *m_fallbackPrefilteredCube, ImageState::ColorShaderRead);

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
			m_fallbackCubeSampler = vk::raii::Sampler(*m_device, samplerCI);
		}

		NEURUS_LOG("[LightingPass] Created fallback IBL cubemaps (4×4 black)");
	}

	// --- Create dummy shadow intensity image (1×1 black R8) for unused array layers ---
	//     Writing binding 9 as a sampler2DArray requires MAX_SHADOW_LIGHTS valid
	//     image descriptors.  Lights that don't cast shadows (or layers beyond
	//     the shadow-casting light count) use this dummy black image.
	{
		vk::Extent2D dummyExtent{1, 1};
		const vk::ImageUsageFlags dummyUsage =
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

		m_dummyShadowImage = std::make_unique<Image>(
			*m_device, *m_physicalDevice, dummyExtent,
			vk::Format::eR8Unorm,
			dummyUsage, /*mipLevels=*/1,
			Image::ImageType::e2D,
			"Lighting_DummyShadow",
			/*arrayView=*/true);

		// Transition UNDEFINED → TransferDst → clear (black) → ColorShaderRead
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

			Barrier::Transition(*cmdBufs[0], *m_dummyShadowImage, ImageState::TransferDst);

			vk::ClearColorValue clearBlack(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
			cmdBufs[0].clearColorImage(*m_dummyShadowImage->ImageHandle(),
			                           vk::ImageLayout::eTransferDstOptimal,
			                           clearBlack,
			                           vk::ImageSubresourceRange(
			                               vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

			Barrier::Transition(*cmdBufs[0], *m_dummyShadowImage, ImageState::ColorShaderRead);

			cmdBufs[0].end();

			vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
			m_graphicsQueue.submit(submitInfo);
			m_graphicsQueue.waitIdle();
		}

		// Sampler: nearest filtering, clamp-to-edge (1x1 — any filter works)
		{
			vk::SamplerCreateInfo samplerCI(
				{}, vk::Filter::eNearest, vk::Filter::eNearest,
				vk::SamplerMipmapMode::eNearest,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				0.0f, VK_FALSE, 0.0f, VK_FALSE,
				vk::CompareOp::eAlways, 0.0f, 0.0f,
				vk::BorderColor::eFloatTransparentBlack, VK_FALSE);
			m_dummyShadowSampler = vk::raii::Sampler(*m_device, samplerCI);
		}

		NEURUS_LOG("[LightingPass] Created dummy R8 shadow image (1×1 black)");
	}

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets; ++i)
	{
		const std::string dsName = "LightingPass_Set" + std::to_string(i);
		m_descriptorSets[i].SetDebugName(dsName.c_str());
	}
#endif
}

LightingPass::~LightingPass() = default;

// ---------------------------------------------------------------------------
// Light SSBO management
// ---------------------------------------------------------------------------

void LightingPass::UploadLights(const Scene& scene,
                               const std::unordered_map<int32_t, int>* shadowIndexMap)
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

		// Shadow map index lookup
		if (shadowIndexMap)
		{
			const auto it = shadowIndexMap->find(id);
			gpu.shadowMapIndex = (it != shadowIndexMap->end()) ? it->second : -1;
		}

		gpuLights.push_back(gpu);
	}

	// Build reverse mapping: shadowMapIndex → lightUID
	// Used by WriteDescriptors to find the shadow intensity image for each array layer.
	m_shadowIndexToUID.clear();
	if (shadowIndexMap)
	{
		for (const auto& [uid, shadowIdx] : *shadowIndexMap)
		{
			m_shadowIndexToUID[static_cast<int32_t>(shadowIdx)] = uid;
		}
	}

	const uint32_t newCount = static_cast<uint32_t>(gpuLights.size());
	m_lightCount = newCount;

	if (newCount == 0)
	{
		m_lightSSBO.reset();
		NEURUS_LOG("[LightingPass] No point lights in scene - SSBO released (PARTIALLY_BOUND)");
		return;
	}

	// Create or re-create the SSBO
	const vk::DeviceSize bufferSize = newCount * sizeof(PointLightGpu);

	m_lightSSBO = std::make_unique<GPUBuffer>(
		*m_device, *m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex,
		bufferSize,
		vk::BufferUsageFlagBits::eStorageBuffer,
		"LightSSBO");
	m_lightSSBO->Upload(gpuLights.data(), bufferSize);

	NEURUS_LOG("[LightingPass] Uploaded " << newCount << " point lights"
	           << " (" << bufferSize << " bytes)");
}

const GPUBuffer* LightingPass::GetLightSSBO() const
{
	return m_lightSSBO ? m_lightSSBO.get() : nullptr;
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
	return BuildLayout()
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
		// Light SSBO (PARTIALLY_BOUND - valid to skip update when no lights)
		.AddBindingWithFlags(5,
		                     vk::DescriptorType::eStorageBuffer,
		                     vk::ShaderStageFlagBits::eCompute,
		                     vk::DescriptorBindingFlagBits::ePartiallyBound)
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
		// Shadow array (sampler2DArray, MAX_SHADOW_LIGHTS layers)
		.AddBinding(9,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute,
		            LightingPass::MAX_SHADOW_LIGHTS)
		.Build(device);
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
		.SetDebugName("LightingPass")
		.AddDescriptorSetLayout(*m_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void LightingPass::WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
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
		const auto& attachment = cache.GetAttachment(gBufferInputs[i], extent);

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
		const auto& hdrColor = cache.GetAttachment(AttachmentName::HDRColor, extent);

		vk::DescriptorImageInfo imageInfo(
			nullptr,                              // sampler (not used for storage images)
			*hdrColor.ImageViewHandle(),          // imageView
			vk::ImageLayout::eGeneral             // imageLayout
		);

		dstSet.WriteImage(4, imageInfo,
		                  vk::DescriptorType::eStorageImage);
	}

	// --- Write light SSBO (skipped when no lights, PARTIALLY_BOUND) ---
	{
		if (m_lightSSBO)
		{
			dstSet.WriteBuffer(5, GetLightSSBO()->GetDescriptorInfo(),
			                   vk::DescriptorType::eStorageBuffer);
		}
		// When m_lightSSBO is nullptr, binding 5 is left un-updated.
		// PARTIALLY_BOUND flag makes this safe because lightCount=0
		// guarantees the shader never accesses binding 5.
	}

	// --- Write SSAO attachment (combined image sampler) ---
	{
		const auto& ssao = cache.GetAttachment(AttachmentName::SSAO, extent);

		vk::DescriptorImageInfo imageInfo(
			*m_sampler,                              // sampler
			*ssao.ImageViewHandle(),                 // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(6, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Write shadow intensity array (binding 9, sampler2DArray) ---
	//     Populates MAX_SHADOW_LIGHTS array elements.  Active shadow lights
	//     map to their shadowMapIndex layer; unused layers use the dummy
	//     1×1 black R8 image to keep the descriptor valid.
	{
		std::array<vk::DescriptorImageInfo, MAX_SHADOW_LIGHTS> shadowInfos;

		for (uint32_t layer = 0; layer < MAX_SHADOW_LIGHTS; ++layer)
		{
			const auto uidIt = m_shadowIndexToUID.find(static_cast<int32_t>(layer));

			if (uidIt != m_shadowIndexToUID.end())
			{
			// Active shadow-casting light — use its shadow intensity image
			auto& intensityImg = cache.GetShadowIntensity(uidIt->second, extent);
			shadowInfos[layer] = vk::DescriptorImageInfo(
				*m_sampler,
				*intensityImg.ImageViewArrayHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			}
			else
			{
		// Unused layer — use dummy 1×1 black image
			shadowInfos[layer] = vk::DescriptorImageInfo(
				*m_dummyShadowSampler,
				*m_dummyShadowImage->ImageViewArrayHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			}
		}

		// Write array descriptor via raw Vulkan API (DescriptorSet::WriteImage
		// only supports single-image writes).
		vk::WriteDescriptorSet writeDesc(
			m_descriptorSets[setIndex].handle(),  // dstSet (vk::DescriptorSet)
			9,                                    // dstBinding
			0,                                    // dstArrayElement
			MAX_SHADOW_LIGHTS,                    // descriptorCount
			vk::DescriptorType::eCombinedImageSampler,
			shadowInfos.data(),                   // pImageInfo
			nullptr,                              // pBufferInfo
			nullptr                               // pTexelBufferView
		);

		m_device->updateDescriptorSets(writeDesc, nullptr);
	}
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void LightingPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	const glm::vec3& cameraPos = ctx.cameraPos;
	const glm::mat4& viewMatrix = ctx.view;
	const glm::mat4& invProjView = ctx.invProjView;
	const vk::Extent2D renderExtent = ctx.renderExtent;
	const uint32_t frameIndex = ctx.frameIndex;

	// --- 1. Write descriptor set for this frame slot ---
	WriteDescriptors(frameIndex, renderExtent, cache);

	// --- 1b. Write IBL cubemap descriptors (bindings 7-8) from scene Environment or fallback ---
	{
		DescriptorSet& dstSet = m_descriptorSets[frameIndex];
		const bool hasEnv = (ctx.scene != nullptr && !ctx.scene->env_list.empty());

		if (hasEnv)
		{
			auto& env = ctx.scene->env_list.begin()->second;
			Texture* diffuseTex = env->GetDiffuseTexture();
			Texture* specularTex = env->GetSpecularTexture();

			if (diffuseTex && diffuseTex->GetImage())
			{
				vk::DescriptorImageInfo irrInfo(
					*diffuseTex->GetSampler(),
					*diffuseTex->GetImage()->ImageViewHandle(),
					vk::ImageLayout::eShaderReadOnlyOptimal);
				dstSet.WriteImage(7, irrInfo,
				                  vk::DescriptorType::eCombinedImageSampler);
			}
			else
			{
				// Diffuse not ready - use fallback
				vk::DescriptorImageInfo fbInfo(
					*m_fallbackCubeSampler,
					*m_fallbackIrradianceCube->ImageViewHandle(),
					vk::ImageLayout::eShaderReadOnlyOptimal);
				dstSet.WriteImage(7, fbInfo,
				                  vk::DescriptorType::eCombinedImageSampler);
			}

			if (specularTex && specularTex->GetImage())
			{
				vk::DescriptorImageInfo specInfo(
					*specularTex->GetSampler(),
					*specularTex->GetImage()->ImageViewHandle(),
					vk::ImageLayout::eShaderReadOnlyOptimal);
				dstSet.WriteImage(8, specInfo,
				                  vk::DescriptorType::eCombinedImageSampler);
			}
			else
			{
				// Specular not ready - use fallback
				vk::DescriptorImageInfo fbInfo(
					*m_fallbackCubeSampler,
					*m_fallbackPrefilteredCube->ImageViewHandle(),
					vk::ImageLayout::eShaderReadOnlyOptimal);
				dstSet.WriteImage(8, fbInfo,
				                  vk::DescriptorType::eCombinedImageSampler);
			}
		}
		else
		{
			// No scene or no environment — bind fallback cubemaps
			vk::DescriptorImageInfo fbIrradInfo(
				*m_fallbackCubeSampler,
				*m_fallbackIrradianceCube->ImageViewHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			dstSet.WriteImage(7, fbIrradInfo,
			                  vk::DescriptorType::eCombinedImageSampler);

			vk::DescriptorImageInfo fbSpecInfo(
				*m_fallbackCubeSampler,
				*m_fallbackPrefilteredCube->ImageViewHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			dstSet.WriteImage(8, fbSpecInfo,
			                  vk::DescriptorType::eCombinedImageSampler);
		}
	}

	// --- 2. Transition all images for compute shader access ---
	//     Uses Barrier::Transition which reads the image's current ImageState
	//     and emits the appropriate vk::ImageMemoryBarrier2.
	{
		const std::array<AttachmentName, 4> gBufferInputs = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,
			AttachmentName::MetallicRoughness,
		};

		for (size_t i = 0; i < 4; ++i)
		{
			auto& attachment = cache.GetAttachment(gBufferInputs[i], renderExtent);
			Barrier::Transition(cmdBuf, attachment, ImageState::ColorShaderRead);
		}

		// HDRColor: current state → ShaderWrite (compute write)
		auto& hdrColor = cache.GetAttachment(AttachmentName::HDRColor, renderExtent);
		Barrier::Transition(cmdBuf, hdrColor, ImageState::ShaderWrite);

		// SSAO: if never written (Undefined), clear to 1.0 (no occlusion), then ShaderRead
		auto& ssao = cache.GetAttachment(AttachmentName::SSAO, renderExtent);
		if (ssao.State() == ImageState::Undefined)
		{
			Barrier::Transition(cmdBuf, ssao, ImageState::TransferDst);

			vk::ClearColorValue clearWhite(std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});
			cmdBuf.clearColorImage(*ssao.ImageHandle(),
			                       vk::ImageLayout::eTransferDstOptimal,
			                       clearWhite,
			                       vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                                                  0, 1, 0, 1));
		}
		Barrier::Transition(cmdBuf, ssao, ImageState::ColorShaderRead);

		// ShadowIntensity: transition all shadow-casting lights' intensity images
		// to ColorShaderRead.  If an image has never been written (Undefined),
		// clear it to black first as a safety net (ShadowIntensityPass should
		// have already written to it, but be defensive).
		if (ctx.scene)
		{
			for (const auto& [uid, light] : ctx.scene->light_list)
			{
				if (!light || light->light_type != LightType::POINTLIGHT || !light->use_shadow)
					continue;

				auto& shadowAtt = cache.GetShadowIntensity(uid, renderExtent);
				if (shadowAtt.State() == ImageState::Undefined)
				{
					Barrier::Transition(cmdBuf, shadowAtt, ImageState::TransferDst);

					vk::ClearColorValue clearBlack(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
					cmdBuf.clearColorImage(*shadowAtt.ImageHandle(),
					                       vk::ImageLayout::eTransferDstOptimal,
					                       clearBlack,
					                       vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
					                                                  0, 1, 0, 1));
				}
				Barrier::Transition(cmdBuf, shadowAtt, ImageState::ColorShaderRead);
			}
		}
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

		// Enable IBL when scene has an environment
		pc.iblEnabled = (ctx.scene && !ctx.scene->env_list.empty()) ? 1 : 0;

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

	// --- 7. Transition HDRColor: General → ColorShaderRead for subsequent passes ---
	{
		auto& hdrColor = cache.GetAttachment(AttachmentName::HDRColor, renderExtent);
		Barrier::Transition(cmdBuf, hdrColor, ImageState::ColorShaderRead);
	}
}

} // namespace neurus
