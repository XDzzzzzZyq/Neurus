/**
 * @file ShadowIntensityPass.cpp
 * @brief Per-pixel point-light shadow intensity compute pass implementation.
 */

#include "RenderCache.h"
#include "passes/ShadowIntensityPass.h"

#include "ComputePipelineBuilder.h"
#include "Image.h"
#include "render/Barrier.h"
#include "RenderContext.h"
#include "shaders/ShaderModule.h"

#include "Log.h"

#include "scene/Light.h"
#include "scene/Scene.h"

#include <stdexcept>
#include <string>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ShadowIntensityPass::ShadowIntensityPass(const vk::raii::Device& device,
                                         const vk::raii::PhysicalDevice& physicalDevice,
                                         uint32_t numSets,
                                         vk::Queue /*graphicsQueue*/,
                                         uint32_t /*queueFamilyIndex*/,
                                         const uint32_t* compSpv,
                                         size_t compSize)
	: ComputePass(device, physicalDevice,
	              // Allocate 2× descriptor sets per in-flight frame so that
	              // the per-light loop can alternate between two sets without
	              // ever updating a currently-bound descriptor set (which would
	              // invalidate the command buffer — see VUID 00059 et al.).
	              ShadowIntensityPass::CreateDescriptorSetLayout(device),
	              numSets * kSetsPerFrameSlot)
	, m_pipeline(CreatePipeline(device, compSpv, compSize))
{
	NEURUS_LOG("[ShadowIntensityPass] compSize=" << compSize
	           << " numSets=" << numSets
	           << " farPlane=" << Light::point_shadow_far
	           << " bias=" << m_bias);

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets * kSetsPerFrameSlot; ++i)
	{
		const std::string dsName = "ShadowIntensityPass_Set" + std::to_string(i);
		m_descriptorSets[i].SetDebugName(dsName.c_str());
	}
#endif
}

// ---------------------------------------------------------------------------
// Descriptor set layout
// ---------------------------------------------------------------------------

DescriptorSetLayout ShadowIntensityPass::CreateDescriptorSetLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		// Binding 0: G-Buffer world-space position (combined image sampler)
		.AddBinding(0,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// Binding 1: Shadow depth cubemap (combined image sampler, samplerCube)
		.AddBinding(1,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// Binding 2: Shadow intensity output (storage image, R8)
		.AddBinding(2,
		            vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(device);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

vk::raii::Pipeline ShadowIntensityPass::CreatePipeline(const vk::raii::Device& device,
                                                        const uint32_t* compSpv,
                                                        size_t compSize)
{
	// --- Create compute shader module from embedded SPIR-V ---
	auto compModule = ShaderModule::FromEmbedded(device, compSpv, compSize);

	// --- Push constant range (lightWorldPos + farPlane + bias + layerIndex = 24 bytes) ---
	// Matches shadow_eval.comp push constant layout:
	//   vec3  lightWorldPos (12 bytes)
	//   float farPlane       (4 bytes)
	//   float bias           (4 bytes)
	//   int   layerIndex     (4 bytes)
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		6 * sizeof(float));   // 24 bytes

	// --- Build compute pipeline ---
	return m_pipelineBuilder->SetShaderStage(compModule, "main")
		.SetDebugName("ShadowIntensityPass")
		.AddDescriptorSetLayout(*m_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void ShadowIntensityPass::WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
{
	DescriptorSet& dstSet = m_descriptorSets[setIndex];

	// --- Binding 0: G-Buffer world-space position (combined image sampler) ---
	{
		const auto& posAtt = cache.GetAttachment(AttachmentName::Position, extent);

		vk::DescriptorImageInfo imageInfo(
			*m_sampler,                              // sampler
			*posAtt.ImageViewHandle(),               // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(0, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Binding 1: Shadow depth cubemap (combined image sampler, samplerCube) ---
	{
		auto& shadowCube = cache.GetShadowMap(m_currentLightUID);

		vk::DescriptorImageInfo imageInfo(
			*m_sampler,                                     // sampler
			*shadowCube.ImageViewHandle(),                  // imageView (cube type, depth aspect)
			vk::ImageLayout::eDepthStencilReadOnlyOptimal   // imageLayout (matches DepthShaderRead)
		);

		dstSet.WriteImage(1, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Binding 2: Shadow intensity output (storage image, R8, 2D_ARRAY) ---
	{
		auto& shadowIntensity = cache.GetShadowIntensityArray(extent);

		vk::DescriptorImageInfo imageInfo(
			nullptr,                               // sampler (not used for storage images)
			*shadowIntensity.ImageViewHandle(),    // imageView (2D_ARRAY)
			vk::ImageLayout::eGeneral              // imageLayout
		);

		dstSet.WriteImage(2, imageInfo,
		                  vk::DescriptorType::eStorageImage);
	}
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void ShadowIntensityPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	const vk::Extent2D renderExtent = ctx.renderExtent;
	const uint32_t    frameIndex   = ctx.frameIndex;

	// --- Early out: no scene ---
	if (!ctx.scene)
	{
		NEURUS_LOG("[ShadowIntensityPass] No scene, skipping");
		return;
	}

	{
		int shadowCount = 0;
		for (const auto& [uid, light] : ctx.scene->light_list)
		{
			if (light && light->use_shadow)
			{
				auto pos = light->GetPosition();
				shadowCount++;
			}
		}
	}

	// --- 1. Transition G-Buffer Position to ColorShaderRead (once for all lights) ---
	{
		auto& posAtt = cache.GetAttachment(AttachmentName::Position, renderExtent);
		Barrier::Transition(cmdBuf, posAtt, ImageState::ColorShaderRead);
	}

	// --- 2. Transition the shadow intensity array to ShaderWrite (once) ---
	{
		auto& shadowArray = cache.GetShadowIntensityArray(renderExtent);
		Barrier::Transition(cmdBuf, shadowArray, ImageState::ShaderWrite);
	}

	// --- 3. Bind compute pipeline (once for all lights) ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);

	// --- 4. Dispatch shadow evaluation for each shadow-casting light ---
	//     Alternates between two descriptor sets per frame slot so that updating
	//     the descriptor for light N never touches the set that is still bound
	//     from light N-1.  Without this alternation, updating a bound descriptor
	//     set invalidates the command buffer (VUID-00059 chain).
	uint32_t lightIndex = 0;
	for (const auto& [uid, light] : ctx.scene->light_list)
	{
		// Skip non-shadow-casting lights
		if (!light || !light->use_shadow) continue;

		m_currentLightUID = uid;

		// --- Transition shadow depth cubemap: post-ShadowDepthPass → DepthShaderRead ---
		{
			auto& shadowCube = cache.GetShadowMap(uid);
			Barrier::Transition(cmdBuf, shadowCube, ImageState::DepthShaderRead);
		}

		// --- Select descriptor set: alternate between 2 sets per frame slot ---
		const uint32_t setIdx = frameIndex * kSetsPerFrameSlot + (lightIndex % kSetsPerFrameSlot);

		// --- Write descriptor set ---
		WriteDescriptors(setIdx, renderExtent, cache);

		// --- Bind descriptor set ---
		cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
		                          *m_pipelineBuilder->pipelineLayout(),
		                          0,
		                          {m_descriptorSets[setIdx].handle()},
		                          {});

		// --- Push constants ---
		{
			struct ShadowEvalPushConstants
			{
				float lightPosX, lightPosY, lightPosZ;
				float farPlane;
				float bias;
				int32_t layerIndex;
			};

			const auto& pos = light->GetPosition();
			const uint32_t layer = cache.GetShadowIntensityLayer(uid, renderExtent);

			ShadowEvalPushConstants pc = {};
			pc.lightPosX  = pos.x;
			pc.lightPosY  = pos.y;
			pc.lightPosZ  = pos.z;
			pc.farPlane   = Light::point_shadow_far;
			pc.bias       = m_bias;
			pc.layerIndex = static_cast<int32_t>(layer);

			cmdBuf.pushConstants<ShadowEvalPushConstants>(
				*m_pipelineBuilder->pipelineLayout(),
				vk::ShaderStageFlagBits::eCompute,
				0,
				pc);
		}

		// --- Dispatch ---
		const uint32_t groupCountX = (renderExtent.width  + 15) / 16;
		const uint32_t groupCountY = (renderExtent.height + 15) / 16;
		cmdBuf.dispatch(groupCountX, groupCountY, 1);

		++lightIndex;
	}

	// --- 5. Transition shadow intensity array: General → ColorShaderRead for lighting pass ---
	{
		auto& shadowArray = cache.GetShadowIntensityArray(renderExtent);
		Barrier::Transition(cmdBuf, shadowArray, ImageState::ColorShaderRead);
	}
}

} // namespace neurus
