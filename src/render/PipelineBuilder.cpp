#include "PipelineBuilder.h"

#include "buffers/BufferLayout.h"
#include "shaders/ShaderModule.h"

#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Shader stages
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::AddShaderStage(
	const ShaderModule& module,
	vk::ShaderStageFlagBits stage,
	const char* entryPoint)
{
	m_stages.push_back(module.GetStageInfo(stage, entryPoint));
	return *this;
}

PipelineBuilder& PipelineBuilder::AddShaderStage(
	const vk::PipelineShaderStageCreateInfo& stageInfo)
{
	m_stages.push_back(stageInfo);
	return *this;
}

// ---------------------------------------------------------------------------
// Vertex input
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetVertexInput(const BufferLayout& layout)
{
	m_vertexBindings.clear();
	m_vertexAttributes.clear();

	m_vertexBindings.push_back(layout.GetBindingDescription());
	m_vertexAttributes = layout.GetAttributeDescriptions();

	m_vertexInput = vk::PipelineVertexInputStateCreateInfo(
		{},
		m_vertexBindings,
		m_vertexAttributes);
	m_vertexInputSet = true;
	return *this;
}

PipelineBuilder& PipelineBuilder::SetVertexInput()
{
	m_vertexBindings.clear();
	m_vertexAttributes.clear();
	m_vertexInput = vk::PipelineVertexInputStateCreateInfo({}, {}, {});
	m_vertexInputSet = true;
	return *this;
}

PipelineBuilder& PipelineBuilder::SetVertexInput(
	const vk::PipelineVertexInputStateCreateInfo& vertexInput)
{
	m_vertexBindings.clear();
	m_vertexAttributes.clear();
	m_vertexInput = vertexInput;
	m_vertexInputSet = true;
	return *this;
}

// ---------------------------------------------------------------------------
// Input assembly
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetInputAssembly(
	vk::PrimitiveTopology topology,
	bool primitiveRestart)
{
	m_inputAssembly = vk::PipelineInputAssemblyStateCreateInfo(
		{}, topology, primitiveRestart ? VK_TRUE : VK_FALSE);
	return *this;
}

// ---------------------------------------------------------------------------
// Rasterization
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetViewMask(uint32_t viewMask)
{
	m_viewMask = viewMask;
	return *this;
}

PipelineBuilder& PipelineBuilder::SetRasterization(
	vk::PolygonMode polygonMode,
	vk::CullModeFlags cullMode,
	vk::FrontFace frontFace,
	float lineWidth)
{
	m_rasterizer = vk::PipelineRasterizationStateCreateInfo(
		{},
		VK_FALSE,  // depthClampEnable
		VK_FALSE,  // rasterizerDiscardEnable
		polygonMode,
		cullMode,
		frontFace,
		VK_FALSE,  // depthBiasEnable
		0.0f,      // depthBiasConstantFactor
		0.0f,      // depthBiasClamp
		0.0f,      // depthBiasSlopeFactor
		lineWidth);
	return *this;
}

// ---------------------------------------------------------------------------
// Multisampling
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetMultisampling(
	vk::SampleCountFlagBits samples,
	bool sampleShadingEnable,
	float minSampleShading)
{
	m_multisample = vk::PipelineMultisampleStateCreateInfo(
		{},
		samples,
		sampleShadingEnable ? VK_TRUE : VK_FALSE,
		minSampleShading);
	return *this;
}

// ---------------------------------------------------------------------------
// Depth / stencil
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetDepthStencil(
	bool depthTest,
	bool depthWrite,
	vk::CompareOp compareOp,
	bool stencilTest)
{
	vk::StencilOpState stencilOpState(
		vk::StencilOp::eKeep,
		vk::StencilOp::eKeep,
		vk::StencilOp::eKeep,
		vk::CompareOp::eAlways,
		0, 0, 0);

	m_depthStencil = vk::PipelineDepthStencilStateCreateInfo(
		{},
		depthTest ? VK_TRUE : VK_FALSE,
		depthWrite ? VK_TRUE : VK_FALSE,
		compareOp,
		VK_FALSE,  // depthBoundsTestEnable
		stencilTest ? VK_TRUE : VK_FALSE,
		stencilOpState,  // front
		stencilOpState,  // back
		0.0f, 0.0f);     // min/max depth bounds
	return *this;
}

PipelineBuilder& PipelineBuilder::ClearDepthStencil()
{
	m_depthStencil.reset();
	return *this;
}

// ---------------------------------------------------------------------------
// Color blending
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::AddColorBlendAttachment(
	const vk::PipelineColorBlendAttachmentState& attachment)
{
	m_colorBlendAttachments.push_back(attachment);
	return *this;
}

PipelineBuilder& PipelineBuilder::SetColorBlendAttachment()
{
	vk::PipelineColorBlendAttachmentState attachment;
	attachment.blendEnable = VK_TRUE;
	attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	attachment.colorBlendOp = vk::BlendOp::eAdd;
	attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	attachment.alphaBlendOp = vk::BlendOp::eAdd;
	attachment.colorWriteMask =
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
		vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	m_colorBlendAttachments.clear();
	m_colorBlendAttachments.push_back(attachment);
	return *this;
}

PipelineBuilder& PipelineBuilder::ClearColorBlendAttachments()
{
	m_colorBlendAttachments.clear();
	return *this;
}

// ---------------------------------------------------------------------------
// Dynamic state
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::AddDynamicState(vk::DynamicState state)
{
	m_dynamicStates.push_back(state);
	return *this;
}

// ---------------------------------------------------------------------------
// Descriptor set layouts
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetDescriptorSetLayouts(
	const std::vector<vk::DescriptorSetLayout>& layouts)
{
	m_descriptorSetLayouts = layouts;
	return *this;
}

// ---------------------------------------------------------------------------
// Push constant ranges
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetPushConstantRanges(
	const std::vector<vk::PushConstantRange>& ranges)
{
	m_pushConstantRanges = ranges;
	return *this;
}

// ---------------------------------------------------------------------------
// Pipeline cache
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetPipelineCache(const vk::raii::PipelineCache* cache)
{
	m_pipelineCache = cache ? **cache : VK_NULL_HANDLE;
	return *this;
}

// ---------------------------------------------------------------------------
// Dynamic rendering attachment formats
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetColorFormats(const std::vector<vk::Format>& formats)
{
	m_colorFormats = formats;
	return *this;
}

PipelineBuilder& PipelineBuilder::SetDepthFormat(vk::Format format)
{
	m_depthFormat = format;
	return *this;
}

PipelineBuilder& PipelineBuilder::SetStencilFormat(vk::Format format)
{
	m_stencilFormat = format;
	return *this;
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

PipelineBuilder& PipelineBuilder::SetDebugName(const char* name)
{
	m_debugName = name ? name : "";
	return *this;
}

// ---------------------------------------------------------------------------
// BuildGraphicsPipeline
// ---------------------------------------------------------------------------

vk::raii::Pipeline PipelineBuilder::BuildGraphicsPipeline(const vk::raii::Device& device)
{
	// --- Validate required fields ---
	if (m_stages.empty())
	{
		throw std::runtime_error(
			"PipelineBuilder::BuildGraphicsPipeline: "
			"no shader stages added - call AddShaderStage() at least once.");
	}

	if (m_colorFormats.empty() && !m_depthFormat.has_value())
	{
		throw std::runtime_error(
			"PipelineBuilder::BuildGraphicsPipeline: "
			"no color or depth format set - call SetColorFormats() or SetDepthFormat().");
	}

	// --- Viewport state (dynamic - count non-zero, pointers null) ---
	vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1, nullptr);

	// --- Dynamic state ---
	m_dynamicState = vk::PipelineDynamicStateCreateInfo({}, m_dynamicStates);

	// --- Color blend state ---
	// If attachments were added, build the state; otherwise, use empty
	if (!m_colorBlendAttachments.empty())
	{
		m_colorBlend = vk::PipelineColorBlendStateCreateInfo(
			{}, VK_FALSE,
			vk::LogicOp::eCopy,
			m_colorBlendAttachments);
	}
	else
	{
		// Empty color blend (no attachments)
		m_colorBlend = vk::PipelineColorBlendStateCreateInfo({}, VK_FALSE,
			vk::LogicOp::eCopy, nullptr);
	}

	// --- Pipeline layout ---
	vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo(
		{},
		m_descriptorSetLayouts,
		m_pushConstantRanges);

	vk::raii::PipelineLayout pipelineLayout(device, pipelineLayoutCreateInfo);

	// --- Dynamic rendering pipeline create info ---
	vk::PipelineRenderingCreateInfo renderingCreateInfo(
		m_viewMask,
		m_colorFormats,
		m_depthFormat.value_or(vk::Format::eUndefined),
		m_stencilFormat.value_or(vk::Format::eUndefined));

	// --- Pointers to optional state structs ---
	const vk::PipelineVertexInputStateCreateInfo* pVertexInput =
		m_vertexInputSet ? &m_vertexInput : nullptr;

	const vk::PipelineDepthStencilStateCreateInfo* pDepthStencil =
		m_depthStencil.has_value() ? &m_depthStencil.value() : nullptr;

	// --- Assemble graphics pipeline ---
	vk::GraphicsPipelineCreateInfo pipelineCreateInfo(
		{},
		m_stages,
		pVertexInput,
		&m_inputAssembly,
		nullptr,       // No tessellation
		&viewportState,
		&m_rasterizer,
		&m_multisample,
		pDepthStencil,
		&m_colorBlend,
		&m_dynamicState,
		*pipelineLayout,
		nullptr,       // No render pass (dynamic rendering)
		0,             // Subpass index (unused)
		nullptr,       // No base pipeline
		-1,            // No base pipeline index
		&renderingCreateInfo);

	auto pipeline = vk::raii::Pipeline(device, nullptr, pipelineCreateInfo);

#ifdef _DEBUG
	if (!m_debugName.empty())
	{
		device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT(
			vk::ObjectType::ePipeline,
			reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*pipeline)),
			m_debugName.c_str()));
	}
#endif

	return pipeline;
}

} // namespace neurus
