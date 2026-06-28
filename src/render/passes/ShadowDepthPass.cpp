/**
 * @file ShadowDepthPass.cpp
 * @brief Point-light shadow depth cubemap pass implementation.
 */

#include "passes/ShadowDepthPass.h"
#include "passes/GeometryPass.h"       // for GeometryRenderItem
#include "RenderCache.h"         // for GetShadowMap
#include "../PipelineBuilder.h"
#include "../shaders/ShaderModule.h"
#include "render/Barrier.h"

#include "scene/Light.h"
#include "scene/Scene.h"

#include "shadow_depth.vert.h"
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
// Descriptor set layout
// ===========================================================================

DescriptorSetLayout ShadowDepthPass::CreateLightLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		.AddBinding(0, vk::DescriptorType::eUniformBuffer,
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
	, m_multiviewPipelineLayout(nullptr)
	, m_multiviewPipeline(nullptr)
	, m_multiviewColorPipelineLayout(nullptr)
	, m_multiviewColorPipeline(nullptr)
{
	m_device = &device;

	m_vtxLayout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);
	m_vtxLayout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);
	m_vtxLayout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);

	createUniforms(device, physicalDevice, graphicsQueue, queueFamilyIndex);
	createSingleFacePipeline(device);
	createMultiviewPipeline(device);
	createMultiviewColorPipeline(device);

	NEURUS_LOG("[ShadowDepthPass] resolution=" << resolution
	           << " UBOsize=" << sizeof(LightUBO));
}

// ===========================================================================
// createUniforms — UBO, descriptor pool & set
// ===========================================================================

void ShadowDepthPass::createUniforms(const vk::raii::Device& device,
                                      const vk::raii::PhysicalDevice& physicalDevice,
                                      vk::Queue queue, uint32_t qfi)
{
	m_ubo = std::make_unique<UniformBuffer<LightUBO>>(
		device, physicalDevice, "ShadowDepthUBO");

	m_layout = CreateLightLayout(device);
	m_pool = DescriptorPool(device, 1, DescriptorPool::CalculatePoolSizes({&m_layout}, 1));
	m_set = std::make_unique<DescriptorSet>(std::move(m_pool.Allocate(m_layout, 1).front()));
	m_set->WriteBuffer(0, m_ubo->GetDescriptorInfo(), vk::DescriptorType::eUniformBuffer);
#ifdef _DEBUG
	m_set->SetDebugName("ShadowDepth_LightSet");
#endif
}

// ===========================================================================
// createSingleFacePipeline — graphics pipeline + layout (6 face passes)
// ===========================================================================

void ShadowDepthPass::createSingleFacePipeline(const vk::raii::Device& device)
{
	auto vertModule = ShaderModule::FromEmbedded(device, shadow_depth_vert_spv, sizeof(shadow_depth_vert_spv));
	auto fragModule = ShaderModule::FromEmbedded(device, shadow_depth_frag_spv, sizeof(shadow_depth_frag_spv));

	std::vector<vk::PushConstantRange> pushRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstants))
	};

	std::vector<vk::DescriptorSetLayout> dslayouts = { *m_layout.layout() };

	PipelineBuilder builder;
	m_pipeline = builder
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(m_vtxLayout)
		.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
		.SetRasterization(vk::PolygonMode::eFill,
		                  vk::CullModeFlagBits::eNone,
		                  vk::FrontFace::eClockwise)
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLess)
		.SetColorFormats({})
		.SetDepthFormat(kDepthFmt)
		.SetDescriptorSetLayouts(dslayouts)
		.SetPushConstantRanges(pushRanges)
		.BuildGraphicsPipeline(device);

	vk::PipelineLayoutCreateInfo layoutCI({}, dslayouts, pushRanges);
	m_pipelineLayout = vk::raii::PipelineLayout(device, layoutCI);
}

// ===========================================================================
// createMultiviewPipeline — graphics pipeline + layout (single pass, viewMask)
// ===========================================================================

void ShadowDepthPass::createMultiviewPipeline(const vk::raii::Device& device)
{
	auto vertModule = ShaderModule::FromEmbedded(device,
		shadow_depth_multiview_vert_spv, sizeof(shadow_depth_multiview_vert_spv));
	auto fragModule = ShaderModule::FromEmbedded(device,
		shadow_depth_frag_spv, sizeof(shadow_depth_frag_spv));

	// Push constant: mat4 model only (64 bytes), no faceIndex needed
	std::vector<vk::PushConstantRange> pushRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4))
	};

	std::vector<vk::DescriptorSetLayout> dslayouts = { *m_layout.layout() };

	PipelineBuilder builder;
	m_multiviewPipeline = builder
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(m_vtxLayout)
		.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
		.SetRasterization(vk::PolygonMode::eFill,
		                  vk::CullModeFlagBits::eNone,
		                  vk::FrontFace::eClockwise)
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLess)
		.SetColorFormats({})
		.SetDepthFormat(kDepthFmt)
		.SetDescriptorSetLayouts(dslayouts)
		.SetPushConstantRanges(pushRanges)
		.BuildGraphicsPipeline(device);

	vk::PipelineLayoutCreateInfo layoutCI({}, dslayouts, pushRanges);
	m_multiviewPipelineLayout = vk::raii::PipelineLayout(device, layoutCI);

	NEURUS_LOG("[ShadowDepthPass] Multiview pipeline created");
}

// ===========================================================================
// createMultiviewColorPipeline — colour+depth pipeline for verification
// ===========================================================================

void ShadowDepthPass::createMultiviewColorPipeline(const vk::raii::Device& device)
{
	auto vertModule = ShaderModule::FromEmbedded(device,
		shadow_depth_multiview_vert_spv, sizeof(shadow_depth_multiview_vert_spv));
	auto fragModule = ShaderModule::FromEmbedded(device,
		depth_to_color_frag_spv, sizeof(depth_to_color_frag_spv));

	std::vector<vk::PushConstantRange> pushRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4))
	};

	std::vector<vk::DescriptorSetLayout> dslayouts = { *m_layout.layout() };

	PipelineBuilder builder;
	m_multiviewColorPipeline = builder
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(m_vtxLayout)
		.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
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
	m_multiviewColorPipelineLayout = vk::raii::PipelineLayout(device, layoutCI);

	NEURUS_LOG("[ShadowDepthPass] Multiview colour+depth pipeline created");
}

// ===========================================================================
// UBO update
// ===========================================================================

void ShadowDepthPass::updateUBO(const glm::vec3& lightPos, float farPlane)
{
	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
	                                        kNearPlane, farPlane);
	const glm::vec3& p = lightPos;

	LightUBO ubo{};
	ubo.faceVP[0] = proj * glm::lookAt(p, p + glm::vec3( 1, 0, 0), glm::vec3( 0,-1, 0));
	ubo.faceVP[1] = proj * glm::lookAt(p, p + glm::vec3(-1, 0, 0), glm::vec3( 0,-1, 0));
	ubo.faceVP[2] = proj * glm::lookAt(p, p + glm::vec3( 0, 1, 0), glm::vec3( 0, 0, 1));
	ubo.faceVP[3] = proj * glm::lookAt(p, p + glm::vec3( 0,-1, 0), glm::vec3( 0, 0,-1));
	ubo.faceVP[4] = proj * glm::lookAt(p, p + glm::vec3( 0, 0, 1), glm::vec3( 0,-1, 0));
	ubo.faceVP[5] = proj * glm::lookAt(p, p + glm::vec3( 0, 0,-1), glm::vec3( 0,-1, 0));
	ubo.lpx = p.x; ubo.lpy = p.y; ubo.lpz = p.z;
	ubo.farPlane = farPlane;

	m_ubo->Upload(ubo);
}

// ===========================================================================
// Record
// ===========================================================================

void ShadowDepthPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	// Guard: skip if no scene
	if (!ctx.scene) { NEURUS_LOG("[ShadowDepthPass] No scene, skipping"); return; }

	{
		int shadowCount = 0;
		for (const auto& [uid, lightPtr] : ctx.scene->light_list)
		{
			if (lightPtr && lightPtr->use_shadow)
			{
				auto pos = lightPtr->GetPosition();
				shadowCount++;
			}
		}
	}

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

		const ShadowMode mode = (lightPtr->light_type == LightType::POINTLIGHT)
			? m_shadowMode : ShadowMode::SingleFace;

		updateUBO(lightPos, farPlane);

		cmdBuf.setViewport(0, viewport);
		cmdBuf.setScissor(0, scissor);

		// Transition cubemap to depth attachment layout (all faces/layers)
		{
			auto& cubemap = cache.GetShadowMap(uid);
			Barrier::Transition(cmdBuf, cubemap, ImageState::DepthAttachment);
		}

		if (mode == ShadowMode::Multiview)
		{
			// --- Multiview: render all 6 faces in a single pass ---
			// Always colour+depth on this GPU - the depth-only pipeline is unreliable

			const auto& pipeline       = m_multiviewColorPipeline;
			const auto& pipelineLayout = m_multiviewColorPipelineLayout;

			cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
			cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
			                          pipelineLayout, 0,
			                          vk::ArrayProxy<const vk::DescriptorSet>(m_set->handle()), {});

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
					cmdBuf.pushConstants<glm::mat4>(pipelineLayout,
					    vk::ShaderStageFlagBits::eVertex, 0, item.pushConstants.model);
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
		}
		else
		{
			// --- SingleFace: 6 sequential passes, one per cubemap face ---

			cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
			cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
			                          m_pipelineLayout, 0,
			                          vk::ArrayProxy<const vk::DescriptorSet>(m_set->handle()), {});

			const vk::ClearValue clearValue = vk::ClearDepthStencilValue(1.0f, 0);

			for (uint32_t face = 0; face < kShadowFaceCount; ++face)
			{
				vk::RenderingAttachmentInfo depthAtt(
					*(cache.GetShadowMap(uid).FaceView(face)),
					vk::ImageLayout::eDepthStencilAttachmentOptimal,
					vk::ResolveModeFlagBits::eNone, nullptr,
					vk::ImageLayout::eUndefined,
					vk::AttachmentLoadOp::eClear,
					vk::AttachmentStoreOp::eStore,
					clearValue);

				vk::RenderingInfo renderInfo(
					{}, {{0, 0}, {m_resolution, m_resolution}},
					1, 0, {}, &depthAtt, nullptr);

				cmdBuf.beginRendering(renderInfo);

				// Push face index at offset 64 (after model)
				const int32_t faceIdx = static_cast<int32_t>(face);
				cmdBuf.pushConstants<int32_t>(m_pipelineLayout,
				                              vk::ShaderStageFlagBits::eVertex,
				                              sizeof(glm::mat4), faceIdx);

				if (ctx.renderItems)
				{
					for (const auto& item : *ctx.renderItems)
					{
						cmdBuf.pushConstants<glm::mat4>(m_pipelineLayout,
						    vk::ShaderStageFlagBits::eVertex, 0, item.pushConstants.model);
						cmdBuf.bindVertexBuffers(0, {item.vertexBuffer}, {vk::DeviceSize{0}});
						cmdBuf.bindIndexBuffer(item.indexBuffer, 0, item.indexType);
						cmdBuf.drawIndexed(item.indexCount, 1, 0, 0, 0);
					}
				}

				cmdBuf.endRendering();
			}
		}

		// Transition cubemap to DepthShaderRead for sampling in subsequent passes
		{
			auto& cubemap = cache.GetShadowMap(uid);
			Barrier::Transition(cmdBuf, cubemap, ImageState::DepthShaderRead);
		}
	}
}

} // namespace neurus
