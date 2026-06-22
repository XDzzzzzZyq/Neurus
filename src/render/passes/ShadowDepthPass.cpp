/**
 * @file ShadowDepthPass.cpp
 * @brief Point-light shadow depth cubemap pass implementation.
 */

#include "passes/ShadowDepthPass.h"
#include "passes/GeometryPass.h"       // for GeometryRenderItem
#include "../PipelineBuilder.h"
#include "../shaders/ShaderModule.h"

#include "shadow_depth.vert.h"
#include "shadow_depth.frag.h"

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
	auto bindings = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eVertex |
		                vk::ShaderStageFlagBits::eFragment)
		.Build();
	return DescriptorSetLayout(device, bindings);
}

// ===========================================================================
// Constructor
// ===========================================================================

ShadowDepthPass::ShadowDepthPass(const vk::raii::Device& device,
                                  const vk::raii::PhysicalDevice& physicalDevice,
                                  vk::Queue graphicsQueue,
                                  uint32_t queueFamilyIndex,
                                  uint32_t resolution,
                                  float farPlane)
	: Pass()
	, m_resolution(resolution)
	, m_farPlane(farPlane)
	, m_cubemap(device, physicalDevice,
	            vk::Extent2D{resolution, resolution},
	            kDepthFmt,
	            vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                vk::ImageUsageFlagBits::eSampled |
	                vk::ImageUsageFlagBits::eTransferSrc,
	            1u, Image::ImageType::eCube,
	            "ShadowDepthCubemap")
	, m_ubo(std::make_unique<VulkanBuffer>(device, physicalDevice,
	           graphicsQueue, queueFamilyIndex,
	           sizeof(LightUBO),
	           vk::BufferUsageFlagBits::eUniformBuffer,
	           vk::MemoryPropertyFlagBits::eHostVisible |
	               vk::MemoryPropertyFlagBits::eHostCoherent,
	           "ShadowDepthUBO"))
	, m_layout(CreateLightLayout(device))
	, m_pool(device, 1, DescriptorPool::CalculatePoolSizes({&m_layout}, 1))
	, m_set(std::move(m_pool.Allocate(m_layout, 1).front()))
	, m_pipelineLayout(nullptr)
	, m_pipeline(nullptr)
{
	m_device = &device;

	m_set.WriteBuffer(0, m_ubo->GetDescriptorInfo(), vk::DescriptorType::eUniformBuffer);
#ifdef _DEBUG
	m_set.SetDebugName("ShadowDepth_LightSet");
#endif

	m_vtxLayout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);
	m_vtxLayout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);
	m_vtxLayout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);

	createFaceViews(device);

	// --- Create pipeline ---
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

	updateUBO();

	NEURUS_LOG("[ShadowDepthPass] resolution=" << resolution << " farPlane=" << farPlane
	           << " lightPos=(" << m_lightPosition.x << "," << m_lightPosition.y << "," << m_lightPosition.z << ")"
	           << " UBOsize=" << sizeof(LightUBO));
}

// ===========================================================================
// Per-face views
// ===========================================================================

void ShadowDepthPass::createFaceViews(const vk::raii::Device& device)
{
	m_faceViews.clear();
	m_faceViews.reserve(kShadowFaceCount);
	for (uint32_t f = 0; f < kShadowFaceCount; ++f)
	{
		vk::ImageViewCreateInfo ci({}, *m_cubemap.ImageHandle(),
		                           vk::ImageViewType::e2D, kDepthFmt,
		                           vk::ComponentMapping(),
		                           vk::ImageSubresourceRange(
		                               vk::ImageAspectFlagBits::eDepth, 0, 1, f, 1));
		m_faceViews.emplace_back(device, ci);
	}
}

// ===========================================================================
// UBO update
// ===========================================================================

void ShadowDepthPass::updateUBO()
{
	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
	                                        kNearPlane, m_farPlane);
	const glm::vec3& p = m_lightPosition;

	LightUBO ubo{};
	ubo.faceVP[0] = proj * glm::lookAt(p, p + glm::vec3( 1, 0, 0), glm::vec3( 0,-1, 0));
	ubo.faceVP[1] = proj * glm::lookAt(p, p + glm::vec3(-1, 0, 0), glm::vec3( 0,-1, 0));
	ubo.faceVP[2] = proj * glm::lookAt(p, p + glm::vec3( 0, 1, 0), glm::vec3( 0, 0, 1));
	ubo.faceVP[3] = proj * glm::lookAt(p, p + glm::vec3( 0,-1, 0), glm::vec3( 0, 0,-1));
	ubo.faceVP[4] = proj * glm::lookAt(p, p + glm::vec3( 0, 0, 1), glm::vec3( 0,-1, 0));
	ubo.faceVP[5] = proj * glm::lookAt(p, p + glm::vec3( 0, 0,-1), glm::vec3( 0,-1, 0));
	ubo.lpx = p.x; ubo.lpy = p.y; ubo.lpz = p.z;
	ubo._pad0 = 0.0f;
	ubo.farPlane = m_farPlane;

	NEURUS_LOG("[ShadowDepthPass] UBO: farPlane=" << m_farPlane
	           << " nearPlane=" << kNearPlane
	           << " lightPos=(" << p.x << "," << p.y << "," << p.z << ")"
	           << " sizeof=" << sizeof(LightUBO));

	void* mapped = m_ubo->Map();
	std::memcpy(mapped, &ubo, sizeof(LightUBO));
	m_ubo->Unmap();
}

void ShadowDepthPass::SetLightPosition(const glm::vec3& position)
{
	m_lightPosition = position;
	updateUBO();
}

// ===========================================================================
// Record — 6 face passes
// ===========================================================================

void ShadowDepthPass::Record(vk::CommandBuffer cmdBuf, const PassContext& ctx)
{
	const size_t itemCount = ctx.renderItems ? ctx.renderItems->size() : 0;
	NEURUS_LOG("[ShadowDepthPass] Record: " << itemCount << " render items, "
	           << m_resolution << "x" << m_resolution << " faces");

	// Transition cubemap to depth attachment layout (all faces/layers)
	{
		vk::ImageMemoryBarrier barrier(
			{}, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*m_cubemap.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth,
			                          0, 1, 0, kShadowFaceCount));
		cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
		                       vk::PipelineStageFlagBits::eLateFragmentTests,
		                       {}, {}, {}, barrier);
		m_cubemap.SetCurrentLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
	}

	const vk::Viewport viewport(0.f, 0.f,
	                            static_cast<float>(m_resolution),
	                            static_cast<float>(m_resolution),
	                            0.f, 1.f);
	const vk::Rect2D scissor({0, 0}, {m_resolution, m_resolution});
	cmdBuf.setViewport(0, viewport);
	cmdBuf.setScissor(0, scissor);

	cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
	                          m_pipelineLayout, 0,
	                          vk::ArrayProxy<const vk::DescriptorSet>(m_set.handle()), {});

	const vk::ClearValue clearValue = vk::ClearDepthStencilValue(1.0f, 0);

	for (uint32_t face = 0; face < kShadowFaceCount; ++face)
	{
		vk::RenderingAttachmentInfo depthAtt(
			*m_faceViews[face],
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

	// Transition cubemap to SHADER_READ_ONLY for sampling
	{
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*m_cubemap.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth,
			                          0, 1, 0, kShadowFaceCount));
		cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
		                       vk::PipelineStageFlagBits::eComputeShader,
		                       {}, {}, {}, barrier);
		m_cubemap.SetCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
	}
}

} // namespace neurus
