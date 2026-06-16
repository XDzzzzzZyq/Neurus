#include "ShaderProgram.h"

#include "Log.h"

namespace neurus {

ShaderProgram::ShaderProgram(const vk::raii::Device& device,
                             const uint32_t* vertSpv, size_t vertSize,
                             const uint32_t* fragSpv, size_t fragSize,
                             vk::Extent2D extent)
{
	// --- Create shader modules from SPIR-V bytecode ---
	vk::ShaderModuleCreateInfo vertCreateInfo({}, vertSize, vertSpv);
	m_vertModule = std::make_unique<vk::raii::ShaderModule>(device, vertCreateInfo);

	vk::ShaderModuleCreateInfo fragCreateInfo({}, fragSize, fragSpv);
	m_fragModule = std::make_unique<vk::raii::ShaderModule>(device, fragCreateInfo);

	// --- Shader stages ---
	vk::PipelineShaderStageCreateInfo vertStage(
		{},
		vk::ShaderStageFlagBits::eVertex,
		**m_vertModule,
		"main"
	);

	vk::PipelineShaderStageCreateInfo fragStage(
		{},
		vk::ShaderStageFlagBits::eFragment,
		**m_fragModule,
		"main"
	);

	std::vector<vk::PipelineShaderStageCreateInfo> stages = {vertStage, fragStage};

	// --- Vertex input state (no vertex buffers for triangle MVP) ---
	vk::PipelineVertexInputStateCreateInfo vertexInput({}, {}, {});

	// --- Input assembly (triangle list) ---
	vk::PipelineInputAssemblyStateCreateInfo inputAssembly(
		{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

	// --- Viewport + scissor (dynamic state) ---
	vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1, nullptr);

	// --- Rasterizer ---
	vk::PipelineRasterizationStateCreateInfo rasterizer(
		{},
		VK_FALSE,  // depthClampEnable
		VK_FALSE,  // rasterizerDiscardEnable
		vk::PolygonMode::eFill,
		vk::CullModeFlagBits::eNone,
		vk::FrontFace::eClockwise,
		VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f  // depth bias
	);

	// --- Multisampling (none for triangle) ---
	vk::PipelineMultisampleStateCreateInfo multisample(
		{}, vk::SampleCountFlagBits::e1);

	// --- Color blend (no blending for solid triangle) ---
	vk::PipelineColorBlendAttachmentState colorBlendAttachment;
	colorBlendAttachment.colorWriteMask =
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
		vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	vk::PipelineColorBlendStateCreateInfo colorBlend(
		{}, VK_FALSE, vk::LogicOp::eCopy, colorBlendAttachment);

	// --- Dynamic states (viewport + scissor) ---
	std::vector<vk::DynamicState> dynamicStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	};

	vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);

	// --- Pipeline layout (empty - no descriptors or push constants for triangle) ---
	vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	m_pipelineLayout = std::make_unique<vk::raii::PipelineLayout>(device, pipelineLayoutCreateInfo);

	// --- Dynamic rendering pipeline create info ---
	vk::Format colorFormats[] = { vk::Format::eB8G8R8A8Srgb };
	vk::PipelineRenderingCreateInfo renderingCreateInfo(
		{},
		colorFormats
		// No depth or stencil attachment
	);

	vk::GraphicsPipelineCreateInfo pipelineCreateInfo(
		{},
		stages,
		&vertexInput,
		&inputAssembly,
		nullptr,  // No tessellation
		&viewportState,
		&rasterizer,
		&multisample,
		nullptr,  // No depth/stencil
		&colorBlend,
		&dynamicState,
		**m_pipelineLayout,
		nullptr,  // No render pass (dynamic rendering)
		0,
		nullptr,  // No base pipeline
		-1,
		&renderingCreateInfo
	);

	m_pipeline = std::make_unique<vk::raii::Pipeline>(device, nullptr, pipelineCreateInfo);

	NEURUS_LOG("[ShaderProgram] extent=" << extent.width << "x" << extent.height
	          << " vertSize=" << vertSize
	          << " fragSize=" << fragSize
	          << " format=" << vk::to_string(colorFormats[0]));
}

ShaderProgram::~ShaderProgram()
{
	// vk::raii handles cleanup automatically
}

} // namespace neurus
