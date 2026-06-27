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
	              ShadowIntensityPass::CreateDescriptorSetLayout(device), numSets)
	, m_pipeline(CreatePipeline(device, compSpv, compSize))
{
	NEURUS_LOG("[ShadowIntensityPass] compSize=" << compSize
	           << " numSets=" << numSets
	           << " farPlane=" << Light::point_shadow_far
	           << " bias=" << m_bias);

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets; ++i)
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

	// --- Push constant range (lightWorldPos.xyz + farPlane + bias = 20 bytes) ---
	// Matches shadow_eval.comp push constant layout:
	//   vec3 lightWorldPos (12 bytes)
	//   float farPlane      (4 bytes)
	//   float bias          (4 bytes)
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		5 * sizeof(float));   // 20 bytes

	// --- Build compute pipeline ---
	return m_pipelineBuilder->SetShaderStage(compModule, "main")
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

	// --- Binding 2: Shadow intensity output (storage image, R8) ---
	{
		auto& shadowIntensity = cache.GetShadowIntensity(m_currentLightUID, extent);

		vk::DescriptorImageInfo imageInfo(
			nullptr,                               // sampler (not used for storage images)
			*shadowIntensity.ImageViewHandle(),    // imageView
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
	const int32_t     lightUID     = ctx.lightUID;

	// --- Early out: no shadow-casting light in this frame ---
	if (lightUID < 0)
	{
		return;
	}

	m_currentLightUID = lightUID;

	// --- 1. Write descriptor set for this frame slot ---
	WriteDescriptors(frameIndex, renderExtent, cache);

	// --- 2. Transition G-Buffer Position to ColorShaderRead ---
	{
		auto& posAtt = cache.GetAttachment(AttachmentName::Position, renderExtent);
		Barrier::Transition(cmdBuf, posAtt, ImageState::ColorShaderRead);
	}

	// --- 3. Transition shadow depth cubemap from DepthAttachment (post-ShadowDepthPass) to DepthShaderRead ---
	{
		auto& shadowCube = cache.GetShadowMap(lightUID);
		Barrier::Transition(cmdBuf, shadowCube, ImageState::DepthShaderRead);
	}

	// --- 4. Transition ShadowIntensity to ShaderWrite ---
	{
		auto& shadowIntensity = cache.GetShadowIntensity(lightUID, renderExtent);
		Barrier::Transition(cmdBuf, shadowIntensity, ImageState::ShaderWrite);
	}

	// --- 5. Look up light world position from scene ---
	float lightPosX = 0.0f;
	float lightPosY = 0.0f;
	float lightPosZ = 0.0f;

	if (ctx.scene)
	{
		auto it = ctx.scene->light_list.find(lightUID);
		if (it != ctx.scene->light_list.end())
		{
			const auto& pos = it->second->GetPosition();
			lightPosX = pos.x;
			lightPosY = pos.y;
			lightPosZ = pos.z;
		}
	}

	// --- 6. Bind compute pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);

	// --- 7. Bind descriptor set ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          *m_pipelineBuilder->pipelineLayout(),
	                          0,                                    // firstSet
	                          {m_descriptorSets[frameIndex].handle()},
	                          {});

	// --- 8. Push constants ---
	{
		struct ShadowEvalPushConstants
		{
			float lightPosX, lightPosY, lightPosZ;
			float farPlane;
			float bias;
		};

		ShadowEvalPushConstants pc = {};
		pc.lightPosX = lightPosX;
		pc.lightPosY = lightPosY;
		pc.lightPosZ = lightPosZ;
		pc.farPlane  = Light::point_shadow_far;
		pc.bias      = m_bias;

		cmdBuf.pushConstants<ShadowEvalPushConstants>(
			*m_pipelineBuilder->pipelineLayout(),
			vk::ShaderStageFlagBits::eCompute,
			0,
			pc);
	}

	// --- 9. Dispatch ---
	const uint32_t groupCountX = (renderExtent.width  + 15) / 16;
	const uint32_t groupCountY = (renderExtent.height + 15) / 16;
	cmdBuf.dispatch(groupCountX, groupCountY, 1);

	// --- 10. Transition ShadowIntensity output: General → ColorShaderRead for lighting pass ---
	{
		auto& shadowIntensity = cache.GetShadowIntensity(lightUID, renderExtent);
		Barrier::Transition(cmdBuf, shadowIntensity, ImageState::ColorShaderRead);
	}
}

} // namespace neurus
