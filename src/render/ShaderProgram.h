#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <vector>
#include <cstdint>

namespace neurus {

/**
 * @brief Loads SPIR-V shader modules and creates a graphics pipeline.
 *
 * Wraps vk::raii::ShaderModule and vk::raii::Pipeline with RAII.
 * Uses VK_KHR_dynamic_rendering for the pipeline creation.
 */
class ShaderProgram
{
public:
	/**
	 * @brief Creates shader modules and a graphics pipeline from embedded SPIR-V.
	 *
	 * @param device Logical device owning the pipeline.
	 * @param vertSpv Pointer to vertex shader SPIR-V bytecode.
	 * @param vertSize Size of vertex shader SPIR-V in bytes.
	 * @param fragSpv Pointer to fragment shader SPIR-V bytecode.
	 * @param fragSize Size of fragment shader SPIR-V in bytes.
	 * @param extent Swapchain extent (used for viewport/scissor defaults).
	 */
	ShaderProgram(const vk::raii::Device& device,
	              const uint32_t* vertSpv, size_t vertSize,
	              const uint32_t* fragSpv, size_t fragSize,
	              vk::Extent2D extent);
	~ShaderProgram();

	// Non-copyable — owns GPU resources
	ShaderProgram(const ShaderProgram&) = delete;
	ShaderProgram& operator=(const ShaderProgram&) = delete;

	// Movable
	ShaderProgram(ShaderProgram&&) noexcept = default;
	ShaderProgram& operator=(ShaderProgram&&) noexcept = default;

	/** @brief The graphics pipeline handle. */
	const vk::raii::Pipeline& pipeline() const { return *m_pipeline; }

	/** @brief The pipeline layout (describes descriptor set + push constant bindings). */
	const vk::raii::PipelineLayout& pipelineLayout() const { return *m_pipelineLayout; }

private:
	std::unique_ptr<vk::raii::ShaderModule> m_vertModule;
	std::unique_ptr<vk::raii::ShaderModule> m_fragModule;
	std::unique_ptr<vk::raii::PipelineLayout> m_pipelineLayout;
	std::unique_ptr<vk::raii::Pipeline> m_pipeline;
};

} // namespace neurus
