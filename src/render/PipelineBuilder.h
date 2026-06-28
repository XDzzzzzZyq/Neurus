#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <optional>
#include <string>
#include <vector>

namespace neurus {

class BufferLayout;
class ShaderModule;

// ---------------------------------------------------------------------------
// PipelineBuilder - Fluent API for constructing graphics pipelines
// ---------------------------------------------------------------------------

/**
 * @brief Fluent builder for VkGraphicsPipelineCreateInfo with
 *        VK_KHR_dynamic_rendering support.
 *
 * Accumulates state via chained method calls and produces a vk::raii::Pipeline
 * on BuildGraphicsPipeline(). Viewport and scissor are always dynamic.
 *
 * Usage:
 *   auto pipeline = PipelineBuilder()
 *       .AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
 *       .AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
 *       .SetVertexInput(layout)
 *       .SetInputAssembly()
 *       .SetRasterization()
 *       .SetMultisampling()
 *       .SetDepthStencil(true, true)
 *       .SetColorBlendAttachment()
 *       .SetColorFormats({ vk::Format::eB8G8R8A8Srgb })
 *       .BuildGraphicsPipeline(device);
 */
class PipelineBuilder
{
public:
	PipelineBuilder() = default;

	// -----------------------------------------------------------------------
	// Shader stages
	// -----------------------------------------------------------------------

	/**
	 * @brief Adds a shader stage from ShaderModule + stage flag.
	 *
	 * Convenience overload that calls ShaderModule::GetStageInfo().
	 *
	 * @param module      Shader module providing the SPIR-V.
	 * @param stage       Vulkan shader stage (e.g. eVertex, eFragment).
	 * @param entryPoint  Entry-point name (default "main").
	 * @return *this for chaining.
	 */
	PipelineBuilder& AddShaderStage(const ShaderModule& module,
	                                vk::ShaderStageFlagBits stage,
	                                const char* entryPoint = "main");

	/**
	 * @brief Adds a pre-built shader stage create-info.
	 * @param stageInfo   Fully populated VkPipelineShaderStageCreateInfo.
	 * @return *this for chaining.
	 */
	PipelineBuilder& AddShaderStage(const vk::PipelineShaderStageCreateInfo& stageInfo);

	// -----------------------------------------------------------------------
	// Vertex input
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets vertex input state from a BufferLayout.
	 *
	 * Calls BufferLayout::GetBindingDescription() and
	 * GetAttributeDescriptions() to derive the VkPipelineVertexInputState.
	 *
	 * @param layout  Vertex attribute layout describing bindings + attributes.
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetVertexInput(const BufferLayout& layout);

	/**
	 * @brief Clears vertex input state (no vertex buffers / attributes).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetVertexInput();

	/**
	 * @brief Sets vertex input state from a raw VkPipelineVertexInputStateCreateInfo.
	 * @param vertexInput  Pre-built vertex input state.
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetVertexInput(const vk::PipelineVertexInputStateCreateInfo& vertexInput);

	// -----------------------------------------------------------------------
	// Input assembly
	// -----------------------------------------------------------------------

	/**
	 * @brief Configures input assembly (primitive topology + restart).
	 *
	 * @param topology         Primitive topology (default: eTriangleList).
	 * @param primitiveRestart Enable primitive restart (default: false).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetInputAssembly(
		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList,
		bool primitiveRestart = false);

	// -----------------------------------------------------------------------
	// Rasterization
	// -----------------------------------------------------------------------
	/**
	 * @brief Sets the view mask for multiview rendering.
	 * 
	 * @param viewMask  Bitmask of views to render to (e.g., 0 for single view, 0x3f for cubemap views).
	 */
	PipelineBuilder& SetViewMask(uint32_t viewMask);

	/**
	 * @brief Configures the rasterization state.
	 *
	 * @param polygonMode Polygon fill mode (default: eFill).
	 * @param cullMode    Face culling mode (default: eNone).
	 * @param frontFace   Front-face winding (default: eClockwise).
	 * @param lineWidth   Line width for wireframe (default: 1.0f).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetRasterization(
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill,
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone,
		vk::FrontFace frontFace = vk::FrontFace::eClockwise,
		float lineWidth = 1.0f);

	// -----------------------------------------------------------------------
	// Multisampling
	// -----------------------------------------------------------------------

	/**
	 * @brief Configures multisampling state.
	 *
	 * @param samples              Sample count (default: e1).
	 * @param sampleShadingEnable  Enable per-sample shading (default: false).
	 * @param minSampleShading     Minimum fraction of sample shading (default: 0.0f).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetMultisampling(
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
		bool sampleShadingEnable = false,
		float minSampleShading = 0.0f);

	// -----------------------------------------------------------------------
	// Depth / stencil
	// -----------------------------------------------------------------------

	/**
	 * @brief Configures depth/stencil testing.
	 *
	 * @param depthTest    Enable depth testing.
	 * @param depthWrite   Enable depth writes.
	 * @param compareOp    Depth comparison operator (default: eLess).
	 * @param stencilTest  Enable stencil testing (default: false).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetDepthStencil(
		bool depthTest,
		bool depthWrite,
		vk::CompareOp compareOp = vk::CompareOp::eLess,
		bool stencilTest = false);

	/**
	 * @brief Removes the depth/stencil state.
	 *
	 * After calling this, the pipeline will have no depth/stencil attachment.
	 * @return *this for chaining.
	 */
	PipelineBuilder& ClearDepthStencil();

	// -----------------------------------------------------------------------
	// Color blending
	// -----------------------------------------------------------------------

	/**
	 * @brief Appends one color blend attachment state.
	 *
	 * @param attachment  VkPipelineColorBlendAttachmentState.
	 * @return *this for chaining.
	 */
	PipelineBuilder& AddColorBlendAttachment(
		const vk::PipelineColorBlendAttachmentState& attachment);

	/**
	 * @brief Sets color blending with standard alpha blending for a single attachment.
	 *
	 * BlendEnable = true, SrcAlphaBlendFactor = eSrcAlpha,
	 * DstAlphaBlendFactor = eOneMinusSrcAlpha, standard write mask.
	 *
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetColorBlendAttachment();

	/**
	 * @brief Clears all color blend attachments.
	 * @return *this for chaining.
	 */
	PipelineBuilder& ClearColorBlendAttachments();

	// -----------------------------------------------------------------------
	// Dynamic state
	// -----------------------------------------------------------------------

	/**
	 * @brief Adds an additional dynamic state beyond viewport + scissor.
	 *
	 * Viewport and scissor are always dynamic and need not be added here.
	 *
	 * @param state  VkDynamicState to add.
	 * @return *this for chaining.
	 */
	PipelineBuilder& AddDynamicState(vk::DynamicState state);

	// -----------------------------------------------------------------------
	// Descriptor set layouts
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the descriptor set layouts used by the pipeline.
	 *
	 * These are used to create the VkPipelineLayout internally.
	 *
	 * @param layouts  Vector of raw VkDescriptorSetLayout handles.
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetDescriptorSetLayouts(
		const std::vector<vk::DescriptorSetLayout>& layouts);

	// -----------------------------------------------------------------------
	// Push constant ranges
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the push-constant ranges used by the pipeline.
	 *
	 * These are baked into the VkPipelineLayout.
	 *
	 * @param ranges  Vector of VkPushConstantRange entries.
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetPushConstantRanges(
		const std::vector<vk::PushConstantRange>& ranges);

	// -----------------------------------------------------------------------
	// Pipeline cache
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets an optional pipeline cache for reuse across builds.
	 * @param cache  Pointer to an existing vk::raii::PipelineCache (may be nullptr).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetPipelineCache(const vk::raii::PipelineCache* cache);

	// -----------------------------------------------------------------------
	// Dynamic rendering attachment formats
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the color attachment formats for dynamic rendering.
	 *
	 * Must be called - one format per color attachment referenced by the
	 * pipeline.
	 *
	 * @param formats  Vector of VkFormat per color attachment.
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetColorFormats(const std::vector<vk::Format>& formats);

	/**
	 * @brief Sets the depth attachment format for dynamic rendering.
	 * @param format  Depth format (e.g. eD32Sfloat).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetDepthFormat(vk::Format format);

	/**
	 * @brief Sets the stencil attachment format for dynamic rendering.
	 * @param format  Stencil format (e.g. eS8Uint).
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetStencilFormat(vk::Format format);

	// -----------------------------------------------------------------------
	// Debug
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets a debug name for the pipeline (applied inside BuildGraphicsPipeline).
	 *
	 * The name is assigned to the VkPipeline object via
	 * vkSetDebugUtilsObjectNameEXT in Debug builds.
	 *
	 * @param name  Human-readable debug name (e.g. "GeometryPass::G-Buffer").
	 * @return *this for chaining.
	 */
	PipelineBuilder& SetDebugName(const char* name);

	// -----------------------------------------------------------------------
	// Build
	// -----------------------------------------------------------------------

	/**
	 * @brief Builds the graphics pipeline from accumulated state.
	 *
	 * Creates a VkPipelineLayout internally from the descriptor set layouts
	 * and push-constant ranges. Appends VkPipelineRenderingCreateInfo for
	 * dynamic rendering. Returns a RAII pipeline.
	 *
	 * @param device  Logical device (must outlive the returned pipeline).
	 * @return Fully constructed vk::raii::Pipeline.
	 * @throws std::runtime_error if required fields are missing or invalid.
	 */
	vk::raii::Pipeline BuildGraphicsPipeline(const vk::raii::Device& device);

private:
	// --- Shader stages ---
	std::vector<vk::PipelineShaderStageCreateInfo> m_stages;

	// --- Vertex input ---
	vk::PipelineVertexInputStateCreateInfo m_vertexInput = {};
	std::vector<vk::VertexInputBindingDescription> m_vertexBindings;
	std::vector<vk::VertexInputAttributeDescription> m_vertexAttributes;
	bool m_vertexInputSet = false;

	// --- Input assembly ---
	vk::PipelineInputAssemblyStateCreateInfo m_inputAssembly =
		vk::PipelineInputAssemblyStateCreateInfo(
			{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

	// --- Rasterization ---
	vk::PipelineRasterizationStateCreateInfo m_rasterizer =
		vk::PipelineRasterizationStateCreateInfo(
			{}, VK_FALSE, VK_FALSE,
			vk::PolygonMode::eFill,
			vk::CullModeFlagBits::eNone,
			vk::FrontFace::eClockwise,
			VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f);

	// --- Multisampling ---
	vk::PipelineMultisampleStateCreateInfo m_multisample =
		vk::PipelineMultisampleStateCreateInfo(
			{}, vk::SampleCountFlagBits::e1);

	// --- Depth / stencil ---
	std::optional<vk::PipelineDepthStencilStateCreateInfo> m_depthStencil;

	// --- Color blending ---
	std::vector<vk::PipelineColorBlendAttachmentState> m_colorBlendAttachments;
	vk::PipelineColorBlendStateCreateInfo m_colorBlend;

	// --- Dynamic states ---
	std::vector<vk::DynamicState> m_dynamicStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	};
	vk::PipelineDynamicStateCreateInfo m_dynamicState;

	// --- Pipeline layout ---
	std::vector<vk::DescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<vk::PushConstantRange> m_pushConstantRanges;

	// --- Pipeline cache ---
	vk::PipelineCache m_pipelineCache = VK_NULL_HANDLE;

	// --- Dynamic rendering ---
	std::vector<vk::Format> m_colorFormats;
	std::optional<vk::Format> m_depthFormat;
	std::optional<vk::Format> m_stencilFormat;

	uint32_t m_viewMask = 0;

	// --- Debug ---
	std::string m_debugName;
};

} // namespace neurus
