#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <string>
#include <vector>

namespace neurus {

// Forward declarations
class ShaderModule;

/**
 * @brief Fluent builder for compute pipelines.
 *
 * Builds a vk::raii::Pipeline configured for compute workloads.
 * Simpler than the graphics pipeline builder - no vertex input,
 * rasterization, or framebuffer state needed.
 *
 * The builder owns the underlying vk::raii::PipelineLayout and must
 * outlive any pipeline it creates (per Vulkan lifetime rules).
 *
 * Usage:
 * @code
 *   ComputePipelineBuilder builder(device);
 *   auto pipeline = builder
 *       .SetShaderStage(computeShader)
 *       .AddDescriptorSetLayout(descLayout)
 *       .AddPushConstantRange(pushRange)
 *       .BuildComputePipeline();
 *   // builder must stay alive while pipeline is in use
 * @endcode
 */
class ComputePipelineBuilder
{
public:
	/**
	 * @brief Creates a builder bound to the given device.
	 * @param device Logical device for pipeline creation (must outlive the builder).
	 */
	explicit ComputePipelineBuilder(const vk::raii::Device& device);

	ComputePipelineBuilder(const ComputePipelineBuilder&) = delete;
	ComputePipelineBuilder& operator=(const ComputePipelineBuilder&) = delete;

	// Movable (pipeline layout ownership moves with the builder)
	ComputePipelineBuilder(ComputePipelineBuilder&&) noexcept = default;
	ComputePipelineBuilder& operator=(ComputePipelineBuilder&&) noexcept = default;

	/**
	 * @brief Sets the compute shader stage.
	 *
	 * @param shader     Compute shader module (must outlive the BuildComputePipeline() call).
	 * @param entryPoint Shader entry point name (default: "main").
	 * @return Reference to this builder for fluent chaining.
	 */
	ComputePipelineBuilder& SetShaderStage(
		const ShaderModule& shader,
		const char* entryPoint = "main");

	/**
	 * @brief Adds a descriptor set layout to the pipeline layout.
	 *
	 * @param layout Raw VkDescriptorSetLayout handle (non-owning).
	 * @return Reference to this builder for fluent chaining.
	 */
	ComputePipelineBuilder& AddDescriptorSetLayout(vk::DescriptorSetLayout layout);

	/**
	 * @brief Adds a push constant range to the pipeline layout.
	 *
	 * @param range Push constant range descriptor.
	 * @return Reference to this builder for fluent chaining.
	 */
	ComputePipelineBuilder& AddPushConstantRange(const vk::PushConstantRange& range);

	/**
	 * @brief Builds the compute pipeline and returns it.
	 *
	 * Creates the pipeline layout from all accumulated descriptor set
	 * layouts and push constant ranges, then creates the compute pipeline.
	 *
	 * Calling BuildComputePipeline() consumes the accumulated layouts/ranges.
	 * Subsequent calls build fresh pipeline layouts.
	 *
	 * @return A valid vk::raii::Pipeline for compute workloads.
	 * @throws vk::SystemError if pipeline creation fails.
	 * @note The builder must outlive the returned pipeline.
	 */
	vk::raii::Pipeline BuildComputePipeline();

	// -----------------------------------------------------------------------
	// Debug
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets a debug name for the pipeline (applied inside BuildComputePipeline).
	 *
	 * The name is assigned to the VkPipeline object via
	 * vkSetDebugUtilsObjectNameEXT in Debug builds.
	 *
	 * @param name  Human-readable debug name (e.g. "SSAOPass").
	 * @return *this for chaining.
	 */
	ComputePipelineBuilder& SetDebugName(const char* name);

	/**
	 * @brief Returns the most recently created pipeline layout.
	 *
	 * Useful for binding descriptor sets at command-recording time.
	 * Valid after the first call to BuildComputePipeline().
	 *
	 * @return The underlying pipeline layout handle.
	 */
	const vk::raii::PipelineLayout& pipelineLayout() const
	{
		return *m_pipelineLayout;
	}

private:
	const vk::raii::Device& m_device;

	// Compute shader stage info - stored before BuildComputePipeline() is called
	vk::PipelineShaderStageCreateInfo m_stageInfo = {};
	bool m_stageSet = false;

	// Pipeline layout components
	std::vector<vk::DescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<vk::PushConstantRange> m_pushConstantRanges;

	// Owned pipeline layout - created in BuildComputePipeline()
	std::unique_ptr<vk::raii::PipelineLayout> m_pipelineLayout;

	// --- Debug ---
	std::string m_debugName;
};

} // namespace neurus
