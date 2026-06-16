#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <stdexcept>
#include <vector>

namespace neurus {

/**
 * @brief Describes the vertex attribute layout for a single vertex buffer binding.
 *
 * Stores a list of vertex input attribute descriptions (location, format, offset).
 * Produces VkVertexInputBindingDescription and VkVertexInputAttributeDescription
 * structures for use in VkPipelineVertexInputStateCreateInfo.
 *
 * @note Configurable — no hardcoded vertex layouts. Callers add attributes
 *       matching their vertex data structure.
 *
 * Usage:
 *   BufferLayout layout;
 *   layout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);   // position
 *   layout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);  // normal
 *   layout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);     // uv
 *   auto bindingDesc = layout.GetBindingDescription();
 *   auto& attrDescs = layout.GetAttributeDescriptions();
 */
class BufferLayout
{
public:
	BufferLayout() = default;

	/**
	 * @brief Add a vertex attribute description.
	 *
	 * @param location Shader input location (must be 0..maxVertexInputAttributes-1).
	 * @param format   Vulkan format for the attribute (e.g. eR32G32B32Sfloat).
	 * @param offset   Byte offset from the start of the vertex.
	 *
	 * @note The stride is automatically updated to account for this attribute.
	 *       Callers typically add attributes in ascending offset order.
	 */
	void AddAttribute(uint32_t location, vk::Format format, uint32_t offset);

	/**
	 * @brief Returns the Vulkan vertex input binding description.
	 *
	 * Uses binding = 0, calculated stride, and per-vertex input rate.
	 *
	 * @return VkVertexInputBindingDescription ready for pipeline creation.
	 */
	vk::VertexInputBindingDescription GetBindingDescription() const;

	/**
	 * @brief Returns a read-only reference to the attribute descriptions.
	 *
	 * @return Vector of VkVertexInputAttributeDescription, in the order added.
	 */
	const std::vector<vk::VertexInputAttributeDescription>& GetAttributeDescriptions() const;

	/**
	 * @brief Returns the total vertex stride in bytes.
	 *
	 * The stride is computed as: last attribute offset + format size.
	 * If no attributes are added, returns 0.
	 *
	 * @return Total byte stride of a single vertex.
	 */
	uint32_t GetStride() const;

	/**
	 * @brief Returns the byte size of a given Vulkan format.
	 *
	 * Covers common vertex attribute formats:
	 *   - eR32Sfloat (4), eR32G32Sfloat (8), eR32G32B32Sfloat (12),
	 *     eR32G32B32A32Sfloat (16)
	 *   - eR8G8B8A8Unorm/Srgb (4), eB8G8R8A8Unorm/Srgb (4)
	 *   - eR16G16Sfloat (4), eR16G16B16A16Sfloat (8)
	 *
	 * @param format Vulkan format to query.
	 * @return Size in bytes.
	 * @throws std::runtime_error for unsupported formats.
	 */
	static uint32_t GetFormatSize(vk::Format format);

	/** @brief Number of attributes currently registered. */
	size_t GetAttributeCount() const { return m_attributes.size(); }

private:
	std::vector<vk::VertexInputAttributeDescription> m_attributes;
};

} // namespace neurus
