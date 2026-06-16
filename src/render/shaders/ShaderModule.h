#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neurus {

/**
 * @brief Wraps a vk::raii::ShaderModule with RAII.
 *
 * Provides factory methods for loading SPIR-V from files on disk
 * or from embedded data (CMake-generated C headers).
 *
 * Ownership: owns a single vk::raii::ShaderModule. Non-copyable, movable.
 */
class ShaderModule
{
public:
	/**
	 * @brief Creates a shader module from SPIR-V bytecode.
	 * @param device Logical device that will own the module.
	 * @param spirv SPIR-V bytecode as a vector of uint32_t words.
	 */
	ShaderModule(const vk::raii::Device& device, const std::vector<uint32_t>& spirv);
	~ShaderModule();

	// Non-copyable — owns GPU resources
	ShaderModule(const ShaderModule&) = delete;
	ShaderModule& operator=(const ShaderModule&) = delete;

	// Movable
	ShaderModule(ShaderModule&&) noexcept = default;
	ShaderModule& operator=(ShaderModule&&) noexcept = default;

	/**
	 * @brief Factory: loads SPIR-V from a .spv binary file.
	 * @param device Logical device.
	 * @param path Path to the .spv file.
	 * @return A new ShaderModule.
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	static ShaderModule FromFile(const vk::raii::Device& device, const std::string& path);

	/**
	 * @brief Factory: wraps SPIR-V from an embedded C header array.
	 *
	 * Matches the format produced by the CMake SpvToHeader.cmake script:
	 *   constexpr uint32_t myShader[] = { ... };
	 *   constexpr size_t myShader_size = sizeof(myShader);
	 *
	 * @param device Logical device.
	 * @param data Pointer to the uint32_t SPIR-V array.
	 * @param size Size of the array in bytes.
	 * @return A new ShaderModule.
	 */
	static ShaderModule FromEmbedded(const vk::raii::Device& device, const uint32_t* data, size_t size);

	/** @brief The underlying vk::raii::ShaderModule handle. */
	const vk::raii::ShaderModule& handle() const { return *m_module; }

	/**
	 * @brief Builds a VkPipelineShaderStageCreateInfo for this shader module.
	 *
	 * Convenience method for use with PipelineBuilder and other pipeline
	 * construction paths. The caller specifies the stage flag and optional
	 * entry point name.
	 *
	 * @param stage       Vulkan shader stage flag (e.g. eVertex, eFragment).
	 * @param entryPoint  Entry-point function name (default "main").
	 * @return Pre-filled VkPipelineShaderStageCreateInfo.
	 */
	vk::PipelineShaderStageCreateInfo GetStageInfo(
		vk::ShaderStageFlagBits stage,
		const char* entryPoint = "main") const;

private:
	std::unique_ptr<vk::raii::ShaderModule> m_module;
};

} // namespace neurus
