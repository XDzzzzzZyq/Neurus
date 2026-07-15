/**
 * @file ShadowIntensityPass.cpp
 * @brief Per-pixel point-light shadow intensity compute pass implementation.
 */

#include "RenderCache.h"
#include "passes/ShadowIntensityPass.h"
#include "render/RenderConfig.h"

#include "ComputePipelineBuilder.h"
#include "Image.h"
#include "render/Barrier.h"
#include "RenderContext.h"
#include "shaders/ShaderModule.h"

#include "Log.h"

#include "scene/Light.h"
#include "scene/Scene.h"

#include "shaders/ComputeShader.h"
#include "shaders/ShaderLibrary.h"

#include <glm/gtc/matrix_transform.hpp>

#include <stdexcept>
#include <string>

namespace neurus {

// ---------------------------------------------------------------------------
// Push constant structs — compile-time verified
// ---------------------------------------------------------------------------

namespace {

// Sun shadow eval push constants (matches sun_shadow_eval.comp layout).
// GLSL layout:  mat4(64B) + float(4B) + int(4B) = 72B
// C++ sizeof:   80B (16-byte alignment padding from glm::mat4).
// Pipeline range must be ≥ sizeof(C++ struct) to avoid VUID-00369.
struct SunShadowEvalPushConstants
{
	glm::mat4 lightViewProj;
	float     bias;
	int32_t   layerIndex;
};

static_assert(sizeof(SunShadowEvalPushConstants) >= 72,
              "SunShadowEvalPushConstants must be at least 72 bytes to match GLSL layout");
static_assert(

	sizeof(SunShadowEvalPushConstants::lightViewProj) == 64,
	"glm::mat4 size mismatch");
static_assert(
	offsetof(SunShadowEvalPushConstants, bias) == 64,
	"bias offset mismatch — must match sun_shadow_eval.comp push constant layout");
static_assert(
	offsetof(SunShadowEvalPushConstants, layerIndex) == 68,
	"layerIndex offset mismatch — must match sun_shadow_eval.comp push constant layout");

} // anon

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ShadowIntensityPass::ShadowIntensityPass(const vk::raii::Device& device,
                                         const vk::raii::PhysicalDevice& physicalDevice,
                                         uint32_t numSets)
	: ComputePass(device, physicalDevice,
	              // Allocate 2× descriptor sets per in-flight frame so that
	              // the per-light loop can alternate between two sets without
	              // ever updating a currently-bound descriptor set (which would
	              // invalidate the command buffer — see VUID 00059 et al.).
	              ShadowIntensityPass::CreateDescriptorSetLayout(device),
	              numSets * kSetsPerFrameSlot)
	, p_sunDescSetLayout(CreateSunDescriptorSetLayout(device))
{
	// --- Load point-light cubemap eval shader via ShaderLibrary ---
	p_pointLightShader = ShaderLibrary::LoadComputeShader(
		"shadow_eval", "res/shaders/compute/shadow_eval.comp");

	if (!p_pointLightShader)
	{
		throw std::runtime_error("[ShadowIntensityPass] Failed to load 'shadow_eval' compute shader");
	}

	if (!p_pointLightShader->CreateModule(device))
	{
		throw std::runtime_error("[ShadowIntensityPass] Failed to create compute shader module for 'shadow_eval'");
	}

	// --- Create point-light pipeline ---
	p_pipeline = std::make_unique<vk::raii::Pipeline>(CreatePipeline(device));

	// --- Load sun-light 2D eval shader via ShaderLibrary ---
	p_sunLightShader = ShaderLibrary::LoadComputeShader(
		"sun_shadow_eval", "res/shaders/compute/sun_shadow_eval.comp");

	if (!p_sunLightShader)
	{
		throw std::runtime_error("[ShadowIntensityPass] Failed to load 'sun_shadow_eval' compute shader");
	}

	if (!p_sunLightShader->CreateModule(device))
	{
		throw std::runtime_error("[ShadowIntensityPass] Failed to create compute shader module for 'sun_shadow_eval'");
	}

	NEURUS_LOG("[ShadowIntensityPass] numSets=" << numSets
	           << " farPlane=" << Light::point_shadow_far);

	// --- Sun pipeline builder (owns the sun pipeline layout) ---
	p_sunPipelineBuilder = std::make_unique<ComputePipelineBuilder>(device);

	// --- Sun compute pipeline (sun_shadow_eval.comp) ---
	const uint32_t sunSetCount = numSets * kSetsPerFrameSlot;
	p_sunPipeline = CreateSunPipeline(device);

	// --- Sun shadow sampler (clamp-to-border, black border for out-of-bounds UV) ---
	p_sunShadowSampler = CreateSunShadowSampler(device, physicalDevice);

	// --- Sun descriptor pool + sets ---
	p_sunDescPool = DescriptorPool(device,
	                               sunSetCount,
	                               DescriptorPool::CalculatePoolSizes({&p_sunDescSetLayout}, sunSetCount));
	p_sunDescSets = p_sunDescPool.Allocate(p_sunDescSetLayout, sunSetCount);

	NEURUS_LOG("[ShadowIntensityPass] Sun pipeline created, "
	           << sunSetCount << " sun descriptor sets allocated");

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets * kSetsPerFrameSlot; ++i)
	{
		const std::string dsName = "ShadowIntensityPass_Set" + std::to_string(i);
		p_descriptorSets[i].SetDebugName(dsName.c_str());
	}
	for (uint32_t i = 0; i < sunSetCount; ++i)
	{
		const std::string dsName = "ShadowIntensityPass_SunSet" + std::to_string(i);
		p_sunDescSets[i].SetDebugName(dsName.c_str());
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
// Sun descriptor set layout (identical bindings types, but for sun 2D path)
// ---------------------------------------------------------------------------

DescriptorSetLayout ShadowIntensityPass::CreateSunDescriptorSetLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		// Binding 0: G-Buffer world-space position (combined image sampler)
		.AddBinding(0,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// Binding 1: Sun shadow depth map 2D (combined image sampler, sampler2D)
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

vk::raii::Pipeline ShadowIntensityPass::CreatePipeline(const vk::raii::Device& device)
{
	// --- Get compute shader module from ShaderLibrary ---
	auto compModule = p_pointLightShader->GetShaderModule(ShaderType::COMPUTE);

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
	return p_pipelineBuilder->SetShaderStage(*compModule, "main")
		.SetDebugName("ShadowIntensityPass")
		.AddDescriptorSetLayout(*p_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Sun pipeline creation (sun_shadow_eval.comp, 72-byte push constants)
// ---------------------------------------------------------------------------

vk::raii::Pipeline ShadowIntensityPass::CreateSunPipeline(const vk::raii::Device& device)
{
	// --- Get compute shader module from ShaderLibrary ---
	auto compModule = p_sunLightShader->GetShaderModule(ShaderType::COMPUTE);

	// --- Push constant range ---
	// The GLSL push constant block is 72 bytes (mat4 64B + float 4B + int 4B),
	// but the C++ struct SunShadowEvalPushConstants is padded to 80 bytes
	// (16‑byte alignment from glm::mat4). The pipeline must accept the full
	// C++ struct size so that pushConstants<T>() does not violate VUID-00369.
	//   mat4  lightViewProj  (64 bytes, offset 0)
	//   float bias           (4 bytes,  offset 64)
	//   int   layerIndex     (4 bytes,  offset 68)
	//   ---- padding         (8 bytes,  offset 72)  <- C++ only
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		20 * sizeof(float));   // 80 bytes — matches sizeof(SunShadowEvalPushConstants)

	// --- Build sun compute pipeline ---
	return p_sunPipelineBuilder->SetShaderStage(*compModule, "main")
		.SetDebugName("ShadowIntensityPass_Sun")
		.AddDescriptorSetLayout(*p_sunDescSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Sun shadow sampler (clamp-to-border, black = unshadowed)
// ---------------------------------------------------------------------------

vk::raii::Sampler ShadowIntensityPass::CreateSunShadowSampler(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& /*physicalDevice*/)
{
	vk::SamplerCreateInfo samplerCI(
		{},                                        // flags
		vk::Filter::eNearest,                      // magFilter
		vk::Filter::eNearest,                      // minFilter
		vk::SamplerMipmapMode::eNearest,            // mipmapMode
		vk::SamplerAddressMode::eClampToBorder,     // addressModeU
		vk::SamplerAddressMode::eClampToBorder,     // addressModeV
		vk::SamplerAddressMode::eClampToBorder,     // addressModeW
		0.0f,                                       // mipLodBias
		VK_FALSE,                                   // anisotropyEnable
		0.0f,                                       // maxAnisotropy
		VK_FALSE,                                   // compareEnable
		vk::CompareOp::eAlways,                     // compareOp
		0.0f,                                       // minLod
		0.0f,                                       // maxLod
		vk::BorderColor::eFloatOpaqueBlack,         // borderColor (0.0 = unshadowed)
		VK_FALSE                                    // unnormalizedCoordinates
	);

	return vk::raii::Sampler(device, samplerCI);
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void ShadowIntensityPass::WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
{
	DescriptorSet& dstSet = p_descriptorSets[setIndex];

	// --- Binding 0: G-Buffer world-space position (combined image sampler) ---
	{
		const auto& posAtt = cache.GetAttachment(AttachmentName::Position, extent);

		vk::DescriptorImageInfo imageInfo(
			*p_sampler,                              // sampler
			*posAtt.ImageViewHandle(),               // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(0, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Binding 1: Shadow depth cubemap (combined image sampler, samplerCube) ---
	{
		LightGPU* lgpu = cache.GetLightGPU(p_currentLightUID);
		if (!lgpu || !lgpu->shadowDepthMap) return;
		auto& shadowCube = *lgpu->shadowDepthMap;

		vk::DescriptorImageInfo imageInfo(
			*p_sampler,                                     // sampler
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
// Sun descriptor writes (2D shadow map at binding 1)
// ---------------------------------------------------------------------------

void ShadowIntensityPass::WriteSunDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
{
	DescriptorSet& dstSet = p_sunDescSets[setIndex];

	// --- Binding 0: G-Buffer world-space position (combined image sampler) ---
	{
		const auto& posAtt = cache.GetAttachment(AttachmentName::Position, extent);

		vk::DescriptorImageInfo imageInfo(
			*p_sampler,                              // sampler (nearest, clamp-to-edge)
			*posAtt.ImageViewHandle(),               // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(0, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Binding 1: Sun shadow depth map (combined image sampler, sampler2D) ---
	{
		LightGPU* slgpu = cache.GetLightGPU(p_currentLightUID);
		if (!slgpu || !slgpu->shadowDepthMap) return;
		auto& sunShadowMap = *slgpu->shadowDepthMap;

		vk::DescriptorImageInfo imageInfo(
			*p_sunShadowSampler,                             // sampler (nearest, clamp-to-border, black)
			*sunShadowMap.ImageViewHandle(),                 // imageView (2D, depth aspect)
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
	const vk::Extent2D renderExtent{ctx.width, ctx.height};
	const uint32_t    frameIndex   = ctx.frameIndex;

	// --- Early out: no scene ---
	if (!ctx.scene)
	{
		NEURUS_LOG("[ShadowIntensityPass] No scene, skipping");
		return;
	}

	// --- Cast scene UID to Scene* for access to Scene-specific members ---
	const auto* scene = static_cast<const Scene*>(ctx.scene);
	const auto* config = static_cast<const RenderConfig*>(ctx.config);
	const float shadowBias = config ? config->r_shadow_bias : 0.0005f;

	{
		int shadowCount = 0;
		for (const auto& [uid, light] : scene->light_list)
		{
			if (light && light->use_shadow && light->is_viewport && light->is_rendered)
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
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *p_pipeline);

	// --- 4. Dispatch shadow evaluation for each shadow-casting light ---
	//     Alternates between two descriptor sets per frame slot so that updating
	//     the descriptor for light N never touches the set that is still bound
	//     from light N-1.  Without this alternation, updating a bound descriptor
	//     set invalidates the command buffer (VUID-00059 chain).
	uint32_t lightIndex = 0;
	for (const auto& [uid, light] : scene->light_list)
	{
		// Skip non-shadow-casting, invisible, and non-point lights (sun lights handled separately)
		if (!light || !light->use_shadow) continue;
		if (!light->is_viewport || !light->is_rendered) continue;
		if (light->light_type != LightType::POINTLIGHT) continue;

		p_currentLightUID = uid;

		// --- Transition shadow depth cubemap: post-ShadowDepthPass → DepthShaderRead ---
		{
			LightGPU* lgpu = cache.GetLightGPU(uid);
			if (!lgpu || !lgpu->shadowDepthMap) continue;
			auto& shadowCube = *lgpu->shadowDepthMap;
			Barrier::Transition(cmdBuf, shadowCube, ImageState::DepthShaderRead);
		}

		// --- Select descriptor set: alternate between 2 sets per frame slot ---
		const uint32_t setIdx = frameIndex * kSetsPerFrameSlot + (lightIndex % kSetsPerFrameSlot);

		// --- Write descriptor set ---
		WriteDescriptors(setIdx, renderExtent, cache);

		// --- Bind descriptor set ---
		cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
		                          *p_pipelineBuilder->pipelineLayout(),
		                          0,
		                          {p_descriptorSets[setIdx].handle()},
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
			pc.bias       = shadowBias;
			pc.layerIndex = static_cast<int32_t>(layer);

			cmdBuf.pushConstants<ShadowEvalPushConstants>(
				*p_pipelineBuilder->pipelineLayout(),
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

	// --- 4b. Dispatch sun (directional) shadow evaluation for each shadow-casting sun light ---
	//        Uses a separate pipeline and descriptor sets for the 2D shadow path.
	uint32_t sunLightIndex = 0;
	for (const auto& [uid, light] : scene->light_list)
	{
		// Only process visible sun lights that cast shadows
		if (!light || !light->use_shadow) continue;
		if (!light->is_viewport || !light->is_rendered) continue;
		if (light->light_type != LightType::SUNLIGHT) continue;

		p_currentLightUID = uid;

		// --- Transition sun shadow depth map: post-ShadowDepthPass → DepthShaderRead ---
		{
			LightGPU* slgpu = cache.GetLightGPU(uid);
			if (!slgpu || !slgpu->shadowDepthMap) continue;
			auto& sunShadowMap = *slgpu->shadowDepthMap;
			Barrier::Transition(cmdBuf, sunShadowMap, ImageState::DepthShaderRead);
		}

		// --- Select sun descriptor set: alternate between 2 sets per frame slot ---
		const uint32_t setIdx = frameIndex * kSetsPerFrameSlot + (sunLightIndex % kSetsPerFrameSlot);

		// --- Write sun descriptor set ---
		WriteSunDescriptors(setIdx, renderExtent, cache);

		// --- Bind sun pipeline + descriptor set ---
		cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *p_sunPipeline);
		cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
		                          *p_sunPipelineBuilder->pipelineLayout(),
		                          0,
		                          {p_sunDescSets[setIdx].handle()},
		                          {});

		// --- Sun push constants (mat4 lightViewProj + bias + layerIndex) ---
		{

			const glm::vec3 lightDir = glm::normalize(light->GetDirection());
			const float field = Light::sun_shadow_field;
			const float nearPlane = Light::sun_shadow_near;
			const float farPlane = Light::sun_shadow_far;

			// Orthographic projection covering ±field around the origin
			const glm::mat4 lightProj = glm::ortho(-field, field, -field, field, nearPlane, farPlane);

		// View matrix: match ShadowDepthPass sun-path convention.
		// World-up = (0,0,1); alternate-up = (1,0,0) for overhead/sun straight-down.
		constexpr glm::vec3 kWorldUp(0.0f, 0.0f, 1.0f);
			constexpr glm::vec3 kAltUp(1.0f, 0.0f, 0.0f);
			const glm::vec3 up = (glm::abs(glm::dot(lightDir, kWorldUp)) > 0.999f)
				? kAltUp : kWorldUp;

			// Use camera target as ortho centre (same as ShadowDepthPass)
			const Camera* activeCam = scene->GetActiveCamera();
			const glm::vec3 center = activeCam->cam_tar;
			const glm::vec3 lightEye = center - lightDir * farPlane;
			const glm::mat4 lightView = glm::lookAt(lightEye, center, up);
			const glm::mat4 lightViewProj = lightProj * lightView;

			const uint32_t layer = cache.GetShadowIntensityLayer(uid, renderExtent);

			SunShadowEvalPushConstants pc = {};
			pc.lightViewProj = lightViewProj;
			pc.bias          = shadowBias;
			pc.layerIndex    = static_cast<int32_t>(layer);

			cmdBuf.pushConstants<SunShadowEvalPushConstants>(
				*p_sunPipelineBuilder->pipelineLayout(),
				vk::ShaderStageFlagBits::eCompute,
				0,
				pc);
		}

		// --- Dispatch ---
		const uint32_t groupCountX = (renderExtent.width  + 15) / 16;
		const uint32_t groupCountY = (renderExtent.height + 15) / 16;
		cmdBuf.dispatch(groupCountX, groupCountY, 1);

		++sunLightIndex;
	}

	// --- 5. Transition shadow intensity array: General → ColorShaderRead for lighting pass ---
	{
		auto& shadowArray = cache.GetShadowIntensityArray(renderExtent);
		Barrier::Transition(cmdBuf, shadowArray, ImageState::ColorShaderRead);
	}
}

} // namespace neurus
