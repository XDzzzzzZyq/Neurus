/**
 * @file ShadowDepthPass.cpp
 * @brief Point-light shadow depth cubemap pass implementation.
 *
 * Changed from host-visible UBO to SSBO + push constants to fix the
 * GPU-synchronisation bug where UpdateUBO() was called for each light in
 * a single command buffer, but the GPU only saw the last write because
 * command execution is deferred.
 *
 * Now:
 *   - 6 static face view-projection matrices live in a device-local SSBO
 *     (computed once from origin with a fixed far plane).
 *   - Per-light data (lightWorldPos + farPlane) is pushed via push constants
 *     (offset 0, 16 bytes).
 *   - Per-draw model matrix is pushed via push constants (offset 16, 64 bytes).
 */

#include "passes/ShadowDepthPass.h"
#include "passes/GeometryPass.h"       // for GeometryRenderItem
#include "RenderCache.h"               // for GetShadowMap
#include "../PipelineBuilder.h"
#include "../shaders/ShaderModule.h"
#include "render/Barrier.h"

#include "scene/Light.h"
#include "scene/Scene.h"

#include "shadow_depth.frag.h"
#include "shadow_depth_multiview.vert.h"
#include "depth_to_color.frag.h"

#include "Log.h"

#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

namespace neurus {

namespace {
	constexpr float kNearPlane = 0.1f;
	constexpr vk::Format kDepthFmt = vk::Format::eD32Sfloat;
} // anon

// ===========================================================================
// Static helpers — 6 cubemap face view-projection matrices from origin
// ===========================================================================

namespace {

/**
 * @brief Computes 6 view-projection matrices for a cubemap from 0,0,0
 *        with the given projection matrix.
 *
 * These are equivalent to:
 *   proj * lookAt(origin, origin + faceDir, faceUp)
 * and are combined with a per-light position offset in the vertex shader
 * via `worldPos - lightWorldPos`.
 */
std::array<glm::mat4, 6> MakeFaceVPs(const glm::mat4& proj)
{
	// Six cubemap face directions (+X, -X, +Y, -Y, +Z, -Z) with matching up vectors
	return {{
		proj * glm::lookAt(glm::vec3(0.0f), glm::vec3( 1, 0, 0), glm::vec3( 0,-1, 0)),  // +X
		proj * glm::lookAt(glm::vec3(0.0f), glm::vec3(-1, 0, 0), glm::vec3( 0,-1, 0)),  // -X
		proj * glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 1, 0), glm::vec3( 0, 0, 1)),  // +Y
		proj * glm::lookAt(glm::vec3(0.0f), glm::vec3( 0,-1, 0), glm::vec3( 0, 0,-1)),  // -Y
		proj * glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 0, 1), glm::vec3( 0,-1, 0)),  // +Z
		proj * glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 0,-1), glm::vec3( 0,-1, 0)),  // -Z
	}};
}

} // anon

// ===========================================================================
// Descriptor set layout (SSBO for face VP matrices)
// ===========================================================================

DescriptorSetLayout ShadowDepthPass::CreateSSBOLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		.AddBinding(0, vk::DescriptorType::eStorageBuffer,
		            vk::ShaderStageFlagBits::eVertex |
		                vk::ShaderStageFlagBits::eFragment)
		.Build(device);
}

// ===========================================================================
// Constructor
// ===========================================================================

ShadowDepthPass::ShadowDepthPass(const vk::raii::Device& device,
                                   const vk::raii::PhysicalDevice& physicalDevice,
                                   vk::Queue graphicsQueue,
                                   uint32_t queueFamilyIndex,
                                   uint32_t resolution)
	: Pass()
	, m_resolution(resolution)
	, m_pipelineLayout(nullptr)
	, m_pipeline(nullptr)
{
	m_device = &device;

	m_vtxLayout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);
	m_vtxLayout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);
	m_vtxLayout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);

	createSSBOResources(device, physicalDevice, graphicsQueue, queueFamilyIndex);
	createPipeline(device);

	NEURUS_LOG("[ShadowDepthPass] resolution=" << resolution
	           << " faceVPSize=" << kFaceVPSize
	           << " staticFarPlane=" << kStaticFarPlane);
}

// ===========================================================================
// createSSBOResources — SSBO, descriptor pool & set
// ===========================================================================

void ShadowDepthPass::createSSBOResources(const vk::raii::Device& device,
                                           const vk::raii::PhysicalDevice& physicalDevice,
                                           vk::Queue queue, uint32_t qfi)
{
	// --- Compute static face VP matrices once ---
	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
	                                        kNearPlane, kStaticFarPlane);
	const auto faceVPs = MakeFaceVPs(proj);

	// --- Upload to device-local storage buffer ---
	m_faceVPs = std::make_unique<GPUBuffer>(
		device, physicalDevice, queue, qfi,
		kFaceVPSize,
		vk::BufferUsageFlagBits::eStorageBuffer,
		"ShadowDepthFaceVPs");
	m_faceVPs->Upload(faceVPs.data(), kFaceVPSize);

	// --- Descriptor layout, pool, and set ---
	m_ssboLayout = CreateSSBOLayout(device);
	m_ssboPool = DescriptorPool(device, 1,
	                            DescriptorPool::CalculatePoolSizes({&m_ssboLayout}, 1));
	m_ssboSet = std::make_unique<DescriptorSet>(
		std::move(m_ssboPool.Allocate(m_ssboLayout, 1).front()));
	m_ssboSet->WriteBuffer(0, m_faceVPs->GetDescriptorInfo(),
	                       vk::DescriptorType::eStorageBuffer);
#ifdef _DEBUG
	m_ssboSet->SetDebugName("ShadowDepth_FaceVPSSBO");
#endif

	NEURUS_LOG("[ShadowDepthPass] SSBO with 6 faceVP matrices uploaded");
}

// ===========================================================================
// createPipeline — multiview colour+depth pipeline (all 6 faces in single pass)
// ===========================================================================

void ShadowDepthPass::createPipeline(const vk::raii::Device& device)
{
	auto vertModule = ShaderModule::FromEmbedded(device,
		shadow_depth_multiview_vert_spv, sizeof(shadow_depth_multiview_vert_spv));
	auto fragModule = ShaderModule::FromEmbedded(device,
		depth_to_color_frag_spv, sizeof(depth_to_color_frag_spv));

	// Push constant range: 80 bytes (lightPos+farPlane at offset 0, model at offset 16)
	// Accessible by both vertex (full struct) and fragment (light data only).
	std::vector<vk::PushConstantRange> pushRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex |
		                      vk::ShaderStageFlagBits::eFragment,
		                      0, kTotalPushSize)
	};

	std::vector<vk::DescriptorSetLayout> dslayouts = { *m_ssboLayout.layout() };

	PipelineBuilder builder;
	m_pipeline = builder
		.SetDebugName("ShadowDepthPass::Multiview")
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(m_vtxLayout)
		.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
		.SetViewMask(0x3f)  // 6 faces of cubemap
		.SetRasterization(vk::PolygonMode::eFill,
		                  vk::CullModeFlagBits::eNone,
		                  vk::FrontFace::eClockwise)
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLessOrEqual)
		.AddColorBlendAttachment(vk::PipelineColorBlendAttachmentState(
			VK_FALSE,
			vk::BlendFactor::eOne, vk::BlendFactor::eZero,
			vk::BlendOp::eAdd,
			vk::BlendFactor::eOne, vk::BlendFactor::eZero,
			vk::BlendOp::eAdd,
			vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA))
		.SetColorFormats({vk::Format::eR32G32B32A32Sfloat})
		.SetDepthFormat(kDepthFmt)
		.SetDescriptorSetLayouts(dslayouts)
		.SetPushConstantRanges(pushRanges)
		.BuildGraphicsPipeline(device);

	vk::PipelineLayoutCreateInfo layoutCI({}, dslayouts, pushRanges);
	m_pipelineLayout = vk::raii::PipelineLayout(device, layoutCI);

	NEURUS_LOG("[ShadowDepthPass] Pipeline created (SSBO + push-constant based)");
}

// ===========================================================================
// Record
// ===========================================================================

void ShadowDepthPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	// Guard: skip if no scene
	if (!ctx.scene) { NEURUS_LOG("[ShadowDepthPass] No scene, skipping"); return; }

	const vk::Viewport viewport(0.f, 0.f,
	                            static_cast<float>(m_resolution),
	                            static_cast<float>(m_resolution),
	                            0.f, 1.f);
	const vk::Rect2D scissor({0, 0}, {m_resolution, m_resolution});

	for (const auto& [uid, lightPtr] : ctx.scene->light_list)
	{
		// Skip lights that don't cast shadows
		if (!lightPtr || !lightPtr->use_shadow) continue;

		const glm::vec3 lightPos = lightPtr->GetPosition();
		const float farPlane = (lightPtr->light_type == LightType::POINTLIGHT)
			? Light::point_shadow_far
			: Light::sun_shadow_far;

		// Only handle POINTLIGHT with shadow cubemaps; skip other types
		if (lightPtr->light_type != LightType::POINTLIGHT) continue;

		// --- Push per-light data (lightWorldPos + farPlane, offset 0, 16 bytes) ---
		{
			LightPushData lp = {};
			lp.lpx = lightPos.x;
			lp.lpy = lightPos.y;
			lp.lpz = lightPos.z;
			lp.farPlane = farPlane;

			cmdBuf.pushConstants<LightPushData>(m_pipelineLayout,
			    vk::ShaderStageFlagBits::eVertex |
			    vk::ShaderStageFlagBits::eFragment,
			    0, lp);
		}

		cmdBuf.setViewport(0, viewport);
		cmdBuf.setScissor(0, scissor);

		// Transition cubemap to depth attachment layout (all faces/layers)
		{
			auto& cubemap = cache.GetShadowMap(uid);
			Barrier::Transition(cmdBuf, cubemap, ImageState::DepthAttachment);
		}

		// --- Render all 6 faces in a single multiview pass ---
		cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
		cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
		                          m_pipelineLayout, 0,
		                          vk::ArrayProxy<const vk::DescriptorSet>(m_ssboSet->handle()), {});

		// --- Transition colour cubemap to ColorAttachment for rendering ---
		{
			auto& colorCube = cache.GetShadowColorMap(uid, {m_resolution, m_resolution});
			Barrier::Transition(cmdBuf, colorCube, ImageState::ColorAttachment);
		}

		// --- Depth attachment ---
		vk::RenderingAttachmentInfo depthAtt(
			cache.GetShadowMap(uid).ArrayView(),
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::ResolveModeFlagBits::eNone, nullptr,
			vk::ImageLayout::eUndefined,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			vk::ClearDepthStencilValue(1.0f, 0));

		// --- Colour attachment: RenderCache colour cubemap ---
		vk::ImageView colorView = cache.GetShadowColorMap(uid, {m_resolution, m_resolution}).ArrayView();

		vk::RenderingAttachmentInfo colorAtt(
			colorView,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ResolveModeFlagBits::eNone, nullptr,
			vk::ImageLayout::eUndefined,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			vk::ClearColorValue(std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}));

		vk::RenderingInfo renderInfo(
			{}, {{0, 0}, {m_resolution, m_resolution}},
			1u, 0x3Fu, colorAtt, &depthAtt, nullptr);

		cmdBuf.beginRendering(renderInfo);

		if (ctx.renderItems)
		{
			for (const auto& item : *ctx.renderItems)
			{
				// Push model matrix at offset 16 (light data at offset 0
				// persists from the per-light push above).
				cmdBuf.pushConstants<glm::mat4>(m_pipelineLayout,
				    vk::ShaderStageFlagBits::eVertex |
				    vk::ShaderStageFlagBits::eFragment,
				    kModelPushOffset, item.pushConstants.model);
				cmdBuf.bindVertexBuffers(0, {item.vertexBuffer}, {vk::DeviceSize{0}});
				cmdBuf.bindIndexBuffer(item.indexBuffer, 0, item.indexType);
				cmdBuf.drawIndexed(item.indexCount, 1, 0, 0, 0);
			}
		}

		cmdBuf.endRendering();

		// Transition colour cubemap to ColorShaderRead for sampling in subsequent passes
		{
			auto& colorCube = cache.GetShadowColorMap(uid, {m_resolution, m_resolution});
			Barrier::Transition(cmdBuf, colorCube, ImageState::ColorShaderRead);
		}

		// Transition cubemap to DepthShaderRead for sampling in subsequent passes
		{
			auto& cubemap = cache.GetShadowMap(uid);
			Barrier::Transition(cmdBuf, cubemap, ImageState::DepthShaderRead);
		}
	}
}

} // namespace neurus
