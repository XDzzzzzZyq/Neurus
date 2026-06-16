#pragma once

#include "VulkanBuffer.h"

#include <vulkan/vulkan_raii.hpp>

namespace neurus {

/**
 * @brief RAII wrapper around VulkanBuffer specialized for index data.
 *
 * Creates a device-local index buffer with INDEX_BUFFER + TRANSFER_DST usage.
 * Uploads host index data immediately upon construction via the staging pattern.
 * Stores metadata: index count and index type (always UINT32).
 *
 * @note Indices are always uint32_t (VK_INDEX_TYPE_UINT32). This avoids
 *       template complexity while supporting large meshes (up to 4B indices).
 *
 * Usage:
 *   IndexBuffer ibo(device, physicalDevice, queue, qfi,
 *                   indexData, dataSize, numIndices);
 *   vk::Buffer handle = ibo.buffer();
 *   uint32_t count = ibo.GetIndexCount();
 *   vk::IndexType type = ibo.GetIndexType();
 */
class IndexBuffer
{
public:
	/**
	 * @brief Creates a device-local index buffer and uploads index data.
	 *
	 * Internally creates a VulkanBuffer with INDEX_BUFFER | TRANSFER_DST usage
	 * and DEVICE_LOCAL memory, then calls Upload() with the provided data.
	 *
	 * @param device           Borrowed logical device (outlives this buffer).
	 * @param physicalDevice   Borrowed physical device for memory queries.
	 * @param queue            Borrowed graphics queue for staging submits.
	 * @param queueFamilyIndex Queue family index for temp command pool creation.
	 * @param data             Pointer to host index data (uint32_t array).
	 * @param size             Total byte size of index data (indexCount * sizeof(uint32_t)).
	 * @param indexCount       Number of indices in the buffer.
	 */
	IndexBuffer(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice,
	            vk::Queue queue,
	            uint32_t queueFamilyIndex,
	            const uint32_t* data,
	            vk::DeviceSize size,
	            uint32_t indexCount);

	// Non-copyable — owns GPU resources
	IndexBuffer(const IndexBuffer&) = delete;
	IndexBuffer& operator=(const IndexBuffer&) = delete;

	// Movable
	IndexBuffer(IndexBuffer&&) noexcept = default;
	IndexBuffer& operator=(IndexBuffer&&) noexcept = default;

	/** @brief Number of indices in the buffer. */
	uint32_t GetIndexCount() const { return m_indexCount; }

	/** @brief Index type — always VK_INDEX_TYPE_UINT32. */
	vk::IndexType GetIndexType() const { return vk::IndexType::eUint32; }

	/** @brief Underlying Vulkan buffer handle for binding. */
	vk::Buffer buffer() const { return m_buffer.buffer(); }

private:
	VulkanBuffer m_buffer;
	uint32_t m_indexCount;
};

} // namespace neurus
