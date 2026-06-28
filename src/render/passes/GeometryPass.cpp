/**
 * @file GeometryPass.cpp
 * @brief G-Buffer geometry pass implementation.
 */

#include "passes/GeometryPass.h"

#include "passes/Pass.h"
#include "RenderCache.h"
#include "render/Barrier.h"
#include "PipelineBuilder.h"
#include "shaders/ShaderModule.h"

#include "Log.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GeometryPass::GeometryPass(const vk::raii::Device& device,
                           const vk::raii::PhysicalDevice& physicalDevice,
                           vk::Queue queue,
                           uint32_t queueFamilyIndex,
                           const uint32_t* vertSpv,
                           size_t vertSize,
                           const uint32_t* fragSpv,
                           size_t fragSize)
	: m_physicalDevice(&physicalDevice)
	// --- Descriptor set layout ---
	, m_cameraLayout(CreateCameraLayout(device))
	// --- Camera UBO (host-visible for per-frame memcpy update) ---
	, m_cameraUBO(device, physicalDevice, "CameraUBO")
	// --- Descriptor pool (1 set, 1 UBO) ---
	, m_descriptorPool(device,
	                   1,
	                   DescriptorPool::CalculatePoolSizes({&m_cameraLayout}, 1))
	// --- Camera descriptor set (allocated from pool) ---
	, m_cameraDescriptorSet(std::move(
	      m_descriptorPool.Allocate(m_cameraLayout, 1).front()))
	, m_pipelineLayout(nullptr)
	, m_pipeline(nullptr)
{
	m_device = &device;

	// --- Write camera UBO to descriptor set ---
	m_cameraDescriptorSet.WriteBuffer(0, m_cameraUBO.GetDescriptorInfo(),
	                                  vk::DescriptorType::eUniformBuffer);

#ifdef _DEBUG
	m_cameraDescriptorSet.SetDebugName("GeometryPass_CameraSet");
#endif

	// --- Build vertex input layout ---
	m_vertexLayout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);   // pos @ 0
	m_vertexLayout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);  // normal @ 12
	m_vertexLayout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);      // uv @ 24

	// --- Create graphics pipeline ---
	m_pipeline = CreatePipeline(device, vertSpv, vertSize, fragSpv, fragSize);

	NEURUS_LOG("[GeometryPass] vertSize=" << vertSize
	          << " fragSize=" << fragSize
	          << " colorAttachments=4"
	          << " depthAttachments=1"
	          << " vertexStride=" << m_vertexLayout.GetStride());
}

// ---------------------------------------------------------------------------
// Descriptor set layout factory
// ---------------------------------------------------------------------------

DescriptorSetLayout GeometryPass::CreateCameraLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		.AddBinding(0,
		            vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eVertex)
		.Build(device);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

vk::raii::Pipeline GeometryPass::CreatePipeline(const vk::raii::Device& device,
                                                const uint32_t* vertSpv,
                                                size_t vertSize,
                                                const uint32_t* fragSpv,
                                                size_t fragSize)
{
	// --- Create shader modules from embedded SPIR-V ---
	auto vertModule = ShaderModule::FromEmbedded(device, vertSpv, vertSize);
	auto fragModule = ShaderModule::FromEmbedded(device, fragSpv, fragSize);

	// --- G-Buffer colour attachment formats ---
	std::vector<vk::Format> colorFormats = {
		vk::Format::eR16G16B16A16Sfloat,  // Position
		vk::Format::eR16G16B16A16Sfloat,  // Normal
		vk::Format::eR8G8B8A8Srgb,        // Albedo
		vk::Format::eR8G8B8A8Unorm,       // MetallicRoughness
	};

	// --- Colour blend: 4 attachments, no blending (write all channels) ---
	vk::PipelineColorBlendAttachmentState blendState;
	blendState.blendEnable = VK_FALSE;
	blendState.colorWriteMask =
		vk::ColorComponentFlagBits::eR |
		vk::ColorComponentFlagBits::eG |
		vk::ColorComponentFlagBits::eB |
		vk::ColorComponentFlagBits::eA;

	// --- Push constant range: model + normalMatrix (vertex stage only) ---
	std::vector<vk::PushConstantRange> pushConstantRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex,
		                      0,
		                      sizeof(PushConstants))
	};

	// --- Descriptor set layout handles ---
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = {
		*m_cameraLayout.layout()
	};

	// --- Build pipeline via PipelineBuilder ---
	PipelineBuilder builder;
	auto pipeline = builder
		.SetDebugName("GeometryPass::G-Buffer")
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(m_vertexLayout)
		.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
		.SetRasterization(vk::PolygonMode::eFill,
		                  vk::CullModeFlagBits::eNone,
		                  vk::FrontFace::eClockwise)
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLess)
		.AddColorBlendAttachment(blendState)
		.AddColorBlendAttachment(blendState)
		.AddColorBlendAttachment(blendState)
		.AddColorBlendAttachment(blendState)
		.SetColorFormats(colorFormats)
		.SetDepthFormat(vk::Format::eD32Sfloat)
		.SetDescriptorSetLayouts(descriptorSetLayouts)
		.SetPushConstantRanges(pushConstantRanges)
		.BuildGraphicsPipeline(device);

	// --- Create matching pipeline layout for binding calls ---
	vk::PipelineLayoutCreateInfo layoutCI({},
	                                       descriptorSetLayouts,
	                                       pushConstantRanges);
	m_pipelineLayout = vk::raii::PipelineLayout(device, layoutCI);

	return pipeline;
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void GeometryPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	// --- Extract per-frame context ---
	const CameraUBOData cameraData{ctx.viewProj, ctx.view};
	const auto& renderItems = *ctx.renderItems;
	const auto& renderExtent = ctx.renderExtent;

	// --- 1. Upload camera data to UBO ---
	m_cameraUBO.Upload(cameraData);

	// --- 2. Collect G-Buffer attachment image views ---
	//     Attachments start in ImageState::Undefined (first frame) or
	//     ImageState::ColorAttachment (subsequent frames from previous pass).
	//     Barrier::Transition reads the current state and emits the appropriate barrier.
	const std::array<AttachmentName, 4> gBufferColorAttachments = {
		AttachmentName::Position,
		AttachmentName::Normal,
		AttachmentName::Albedo,
		AttachmentName::MetallicRoughness,
	};

	std::vector<vk::ImageView> colorViews;
	colorViews.reserve(4);
	for (const auto& attName : gBufferColorAttachments)
	{
		auto& att = cache.GetAttachment(attName, renderExtent);
		Barrier::Transition(cmdBuf, att, ImageState::ColorAttachment);
		colorViews.push_back(*att.ImageViewHandle());
	}

	auto& depthAtt = cache.GetAttachment(AttachmentName::Depth, renderExtent);
	Barrier::Transition(cmdBuf, depthAtt, ImageState::DepthAttachment);
	vk::ImageView depthView = *depthAtt.ImageViewHandle();

	// --- 3. Begin G-Buffer dynamic rendering pass ---
	const auto clearValues = Pass::PresetClearValues(Pass::PassType::G_BUFFER);

	BeginPass(cmdBuf,
	          colorViews,
	          &depthView,
	          clearValues,
	          renderExtent);

	// --- 4. Set viewport and scissor (dynamic state) ---
	const vk::Viewport viewport(0.0f, 0.0f,
	                            static_cast<float>(renderExtent.width),
	                            static_cast<float>(renderExtent.height),
	                            0.0f, 1.0f);
	cmdBuf.setViewport(0, viewport);

	const vk::Rect2D scissor({0, 0}, renderExtent);
	cmdBuf.setScissor(0, scissor);

	// --- 5. Bind pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);

	// --- 6. Bind camera descriptor set (set 0) ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
	                          *m_pipelineLayout,
	                          0,                           // firstSet
	                          {m_cameraDescriptorSet.handle()},
	                          {});

	// --- 7. Draw each render item ---
	for (const auto& item : renderItems)
	{
		// Push per-draw constants (model + normalMatrix)
		cmdBuf.pushConstants<PushConstants>(*m_pipelineLayout,
		                                    vk::ShaderStageFlagBits::eVertex,
		                                    0,
		                                    item.pushConstants);

		// Bind vertex buffer
		const vk::DeviceSize vbOffset = 0;
		cmdBuf.bindVertexBuffers(0, item.vertexBuffer, vbOffset);

		// Bind index buffer
		cmdBuf.bindIndexBuffer(item.indexBuffer, 0, item.indexType);

		// Draw indexed
		cmdBuf.drawIndexed(item.indexCount, 1, 0, 0, 0);
	}

	// --- 8. End dynamic rendering pass ---
	EndPass(cmdBuf);
}

// ---------------------------------------------------------------------------
// BeginPass / EndPass (G_BUFFER-specific, moved from RenderPassManager)
// ---------------------------------------------------------------------------

void GeometryPass::BeginPass(vk::CommandBuffer cmdBuf,
                              std::span<const vk::ImageView> colorImageViews,
                              const vk::ImageView* pDepthImageView,
                              std::span<const vk::ClearValue> clearValues,
                              vk::Extent2D renderExtent)
{
	const uint32_t colorCount = static_cast<uint32_t>(colorImageViews.size());

	if (colorCount != Pass::ColorAttachmentCount(Pass::PassType::G_BUFFER))
	{
		throw std::invalid_argument(
			"GeometryPass::BeginPass: expected 4 color attachments, got "
			+ std::to_string(colorCount));
	}

	const bool depthProvided = (pDepthImageView != nullptr);
	if (!depthProvided)
	{
		throw std::invalid_argument(
			"GeometryPass::BeginPass: depth attachment required for G_BUFFER pass");
	}

	// --- Build color attachment infos ---
	const auto colorLoadOp  = Pass::ColorLoadOpFor(Pass::PassType::G_BUFFER);
	const auto colorStoreOp = Pass::ColorStoreOpFor(Pass::PassType::G_BUFFER);

	std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos;
	colorAttachmentInfos.reserve(colorCount);

	for (uint32_t i = 0; i < colorCount; ++i)
	{
		colorAttachmentInfos.push_back(vk::RenderingAttachmentInfo(
			colorImageViews[i],
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ResolveModeFlagBits::eNone,
			nullptr,
			vk::ImageLayout::eUndefined,
			colorLoadOp,
			colorStoreOp,
			(i < static_cast<uint32_t>(clearValues.size())) ? clearValues[i] : vk::ClearValue{}));
	}

	// --- Build depth attachment info ---
	const auto depthLoadOp  = Pass::DepthLoadOpFor(Pass::PassType::G_BUFFER);
	const auto depthStoreOp = Pass::DepthStoreOpFor(Pass::PassType::G_BUFFER);

	const size_t depthClearIndex = colorCount;

	vk::RenderingAttachmentInfo depthAttachmentInfo(
		*pDepthImageView,
		vk::ImageLayout::eDepthStencilAttachmentOptimal,
		vk::ResolveModeFlagBits::eNone,
		nullptr,
		vk::ImageLayout::eUndefined,
		depthLoadOp,
		depthStoreOp,
		(depthClearIndex < clearValues.size()) ? clearValues[depthClearIndex] : vk::ClearValue{});

	// --- Build rendering info ---
	vk::RenderingInfo renderingInfo(
		{},
		vk::Rect2D({0, 0}, renderExtent),
		1,
		0,
		colorAttachmentInfos,
		&depthAttachmentInfo,
		nullptr);

	cmdBuf.beginRendering(renderingInfo);
}

void GeometryPass::EndPass(vk::CommandBuffer cmdBuf)
{
	cmdBuf.endRendering();
}

} // namespace neurus
