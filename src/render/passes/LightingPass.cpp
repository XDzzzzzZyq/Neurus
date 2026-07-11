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
#include "../resources/LightingGPU.h"
#include "shaders/ShaderLibrary.h"
#include "shaders/ComputeShader.h"
#include "Texture.h"

#include "Log.h"

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
                           uint32_t queueFamilyIndex)
	: ComputePass(device, physicalDevice,
	              LightingPass::CreateDescriptorSetLayout(device), numSets)
	, p_pipeline(nullptr)
	// --- Self-load compute shader via ShaderLibrary ---
	, p_computeShader(
		ShaderLibrary::LoadComputeShader("pbr_lighting",
		                                "res/shaders/compute/pbr_lighting.comp"))
{
	// --- Create module from self-loaded shader ---
	if (p_computeShader) { p_computeShader->CreateModule(device); }

	// --- Create pipeline from self-loaded shader ---
	p_pipeline = CreatePipeline(device);

	NEURUS_LOG("[LightingPass] numSets=" << numSets << " qfi=" << queueFamilyIndex
	           << " shader=" << (p_computeShader ? "OK" : "FAIL"));

	// --- Create fallback IBL cubemaps (4×4 black) for bindings 8-9 ---
	//     These ensure the descriptor bindings are always valid even when
	//     no Environment is present in the scene (no IBL to sample).
	//     Using 4×4 faces to satisfy minimum cubemap dimension requirements.
	{
		const vk::ImageUsageFlags cubeUsage =
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		const vk::Extent2D fbExtent{4, 4};

		// --- Fallback diffuse irradiance cubemap ---
		p_fallbackIrradianceCube = std::make_unique<Image>(
			*p_device, *p_physicalDevice, fbExtent,
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage, /*mipLevels=*/1,
			Image::ImageType::eCube,
			"Lighting_IrradianceFallback");

		// --- Fallback specular prefiltered cubemap ---
		p_fallbackPrefilteredCube = std::make_unique<Image>(
			*p_device, *p_physicalDevice, fbExtent,
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage, /*mipLevels=*/1,
			Image::ImageType::eCube,
			"Lighting_PrefilteredFallback");

		// Transition fallback cubemaps from UNDEFINED to SHADER_READ_ONLY_OPTIMAL
		// so that the descriptor image layout matches the actual image layout.
		{
			vk::Queue graphicsQueue = device.getQueue(queueFamilyIndex, 0);

			vk::CommandPoolCreateInfo poolCI(
				vk::CommandPoolCreateFlagBits::eTransient,
				queueFamilyIndex);
			vk::raii::CommandPool cmdPool(*p_device, poolCI);

			vk::CommandBufferAllocateInfo allocInfo(
				*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
			vk::raii::CommandBuffers cmdBufs(*p_device, allocInfo);

			cmdBufs[0].begin(vk::CommandBufferBeginInfo(
				vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			Barrier::Transition(*cmdBufs[0], *p_fallbackIrradianceCube, ImageState::ColorShaderRead);

			Barrier::Transition(*cmdBufs[0], *p_fallbackPrefilteredCube, ImageState::ColorShaderRead);

			cmdBufs[0].end();

			vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
			graphicsQueue.submit(submitInfo);
			graphicsQueue.waitIdle();
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
			p_fallbackCubeSampler = vk::raii::Sampler(*p_device, samplerCI);
		}

		NEURUS_LOG("[LightingPass] Created fallback IBL cubemaps (4×4 black)");
	}

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets; ++i)
	{
		const std::string dsName = "LightingPass_Set" + std::to_string(i);
		p_descriptorSets[i].SetDebugName(dsName.c_str());
	}
#endif
}

LightingPass::~LightingPass() = default;

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
		// SunLight SSBO (PARTIALLY_BOUND - valid to skip update when no sun lights)
		.AddBindingWithFlags(6,
		                     vk::DescriptorType::eStorageBuffer,
		                     vk::ShaderStageFlagBits::eCompute,
		                     vk::DescriptorBindingFlagBits::ePartiallyBound)
		// SSAO occlusion input (combined image sampler)
		.AddBinding(7,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// IBL diffuse irradiance cubemap (combined image sampler)
		.AddBinding(8,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// IBL specular prefiltered cubemap (combined image sampler)
		.AddBinding(9,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// Shadow array (sampler2DArray, single layered image)
		.AddBinding(10,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(device);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

vk::raii::Pipeline LightingPass::CreatePipeline(const vk::raii::Device& device)
{
	// --- Guard: shader must be valid ---
	if (!p_computeShader || !p_computeShader->IsValid())
	{
		throw std::runtime_error("LightingPass: Compute shader not loaded or invalid");
	}

	// --- Use self-loaded compute shader module ---
	auto compModule = p_computeShader->GetShaderModule(ShaderType::COMPUTE);

	// --- Push constant range ---
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		sizeof(LightingPushConstants));  // 176 bytes, from LightingGPU.h

	// --- Build compute pipeline ---
	return p_pipelineBuilder->SetShaderStage(*compModule, "main")
		.SetDebugName("LightingPass")
		.AddDescriptorSetLayout(*p_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void LightingPass::WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
{
	DescriptorSet& dstSet = p_descriptorSets[setIndex];

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
			*p_sampler,                              // sampler
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

	// --- Write light SSBO (from RenderCache::LightingGPU) ---
	{
		const auto* lightingGPU = cache.GetLightingGPU();
		const GPUBuffer* pointSSBO = lightingGPU ? lightingGPU->GetPointLightSSBO() : nullptr;

		if (pointSSBO)
		{
			dstSet.WriteBuffer(5, pointSSBO->GetDescriptorInfo(),
			                   vk::DescriptorType::eStorageBuffer);
		}
		// When pointSSBO is nullptr, binding 5 is left un-updated.
		// PARTIALLY_BOUND flag makes this safe because lightCount=0
		// guarantees the shader never accesses binding 5.
	}

	// --- Write sun light SSBO (from RenderCache::LightingGPU) ---
	{
		const auto* lightingGPU = cache.GetLightingGPU();
		const GPUBuffer* sunSSBO = lightingGPU ? lightingGPU->GetSunLightSSBO() : nullptr;

		if (sunSSBO)
		{
			dstSet.WriteBuffer(6, sunSSBO->GetDescriptorInfo(),
			                   vk::DescriptorType::eStorageBuffer);
		}
		// When sunSSBO is nullptr, PARTIALLY_BOUND makes this safe
		// because sunLightCount=0 guarantees the shader never accesses binding 6.
	}

	// --- Write SSAO attachment (combined image sampler) ---
	{
		const auto& ssao = cache.GetAttachment(AttachmentName::SSAO, extent);

		vk::DescriptorImageInfo imageInfo(
			*p_sampler,                              // sampler
			*ssao.ImageViewHandle(),                 // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(7, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Write shadow intensity array (binding 10, sampler2DArray) ---
	//     Single layered image — no per-layer dummy images needed.
	{
		auto& shadowArray = cache.GetShadowIntensityArray(extent);

		vk::DescriptorImageInfo imageInfo(
			*p_sampler,
			*shadowArray.ImageViewHandle(),           // 2D_ARRAY view
			vk::ImageLayout::eShaderReadOnlyOptimal);

		dstSet.WriteImage(10, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
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

	// --- 1b. Write IBL cubemap descriptors (bindings 8-9) from RenderCache EnvironmentGPU or fallback ---
	{
		DescriptorSet& dstSet = p_descriptorSets[frameIndex];
		const bool hasEnv = (ctx.scene != nullptr && !ctx.scene->env_list.empty());
		const EnvironmentGPU* envGPU = nullptr;

		if (hasEnv)
		{
			auto& env = ctx.scene->env_list.begin()->second;
			envGPU = cache.GetEnvironmentGPU(env->GetObjectID());
		}

		if (envGPU && envGPU->diffuseTexture && envGPU->diffuseTexture->GetImage())
		{
			vk::DescriptorImageInfo irrInfo(
				*envGPU->diffuseTexture->GetSampler(),
				*envGPU->diffuseTexture->GetImage()->ImageViewHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			dstSet.WriteImage(8, irrInfo,
			                  vk::DescriptorType::eCombinedImageSampler);
		}
		else
		{
			// Diffuse not ready - use fallback
			vk::DescriptorImageInfo fbInfo(
				*p_fallbackCubeSampler,
				*p_fallbackIrradianceCube->ImageViewHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			dstSet.WriteImage(8, fbInfo,
			                  vk::DescriptorType::eCombinedImageSampler);
		}

		if (envGPU && envGPU->specularTexture && envGPU->specularTexture->GetImage())
		{
			vk::DescriptorImageInfo specInfo(
				*envGPU->specularTexture->GetSampler(),
				*envGPU->specularTexture->GetImage()->ImageViewHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			dstSet.WriteImage(9, specInfo,
			                  vk::DescriptorType::eCombinedImageSampler);
		}
		else
		{
			// Specular not ready - use fallback
			vk::DescriptorImageInfo fbInfo(
				*p_fallbackCubeSampler,
				*p_fallbackPrefilteredCube->ImageViewHandle(),
				vk::ImageLayout::eShaderReadOnlyOptimal);
			dstSet.WriteImage(9, fbInfo,
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

		// ShadowIntensity: transition the whole intensity array to ColorShaderRead.
		{
			auto& shadowArray = cache.GetShadowIntensityArray(renderExtent);
			if (shadowArray.State() == ImageState::Undefined)
			{
				Barrier::Transition(cmdBuf, shadowArray, ImageState::TransferDst);

				vk::ClearColorValue clearBlack(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
				for (uint32_t layer = 0; layer < RenderCache::MAX_SHADOW_LAYERS; ++layer)
				{
					cmdBuf.clearColorImage(*shadowArray.ImageHandle(),
					                       vk::ImageLayout::eTransferDstOptimal,
					                       clearBlack,
					                       vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
					                                                  0, 1, layer, 1));
				}
			}
			Barrier::Transition(cmdBuf, shadowArray, ImageState::ColorShaderRead);
		}
	}

	// --- 3. Bind compute pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *p_pipeline);

	// --- 4. Bind descriptor set ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          *p_pipelineBuilder->pipelineLayout(),
	                          0,                                    // firstSet
	                          {p_descriptorSets[frameIndex].handle()},
	                          {});

	// --- 5. Push constants ---
	{
		LightingPushConstants pc = {};

		// Get light counts from RenderCache::LightingGPU
		const auto* lightingGPU = cache.GetLightingGPU();
		pc.lightCount    = lightingGPU ? static_cast<int32_t>(lightingGPU->GetPointLightCount()) : 0;
		pc.sunLightCount = lightingGPU ? static_cast<int32_t>(lightingGPU->GetSunLightCount()) : 0;

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
			*p_pipelineBuilder->pipelineLayout(),
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
