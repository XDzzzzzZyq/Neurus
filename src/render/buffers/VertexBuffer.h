#pragma once

#include "VulkanBuffer.h"

#include <vulkan/vulkan_raii.hpp>

namespace neurus {

/**
 * @brief RAII wrapper around VulkanBuffer specialized for vertex data.
 *
 * Creates a device-local vertex buffer with VERTEX_BUFFER + TRANSFER_DST usage.
 * Uploads host vertex data immediately upon construction via the staging pattern.
 * Stores metadata: vertex count and per-vertex stride in bytes.
 *
 * @note Template-free design — vertex data is passed as raw bytes + stride.
 *       Callers must ensure the data pointer remains valid during construction.
 *
 * Usage:
 *   VertexBuffer vbo(device, physicalDevice, queue, qfi,
 *                    vertexData, dataSize, sizeof(Vertex), numVertices);
 *   vk::Buffer handle = vbo.buffer();
 *   uint32_t count = vbo.GetVertexCount();
 */
class VertexBuffer
{
public:
	/**
	 * @brief Creates a device-local vertex buffer and uploads vertex data.
	 *
	 * Internally creates a VulkanBuffer with VERTEX_BUFFER | TRANSFER_DST usage
	 * and DEVICE_LOCAL memory, then calls Upload() with the provided data.
	 *
	 * @param device           Borrowed logical device (outlives this buffer).
	 * @param physicalDevice   Borrowed physical device for memory queries.
	 * @param queue            Borrowed graphics queue for staging submits.
	 * @param queueFamilyIndex Queue family index for temp command pool creation.
	 * @param data             Pointer to host vertex data (raw bytes).
	 * @param size             Total byte size of vertex data (vertexCount * stride).
	 * @param stride           Byte stride between consecutive vertices.
	 * @param vertexCount      Number of vertices (must match size / stride).
	 */
	VertexBuffer(const vk::raii::Device& device,
	             const vk::raii::PhysicalDevice& physicalDevice,
	             vk::Queue queue,
	             uint32_t queueFamilyIndex,
	             const void* data,
	             vk::DeviceSize size,
	             uint32_t stride,
	             uint32_t vertexCount);

	// Non-copyable — owns GPU resources
	VertexBuffer(const VertexBuffer&) = delete;
	VertexBuffer& operator=(const VertexBuffer&) = delete;

	// Movable
	VertexBuffer(VertexBuffer&&) noexcept = default;
	VertexBuffer& operator=(VertexBuffer&&) noexcept = default;

	/** @brief Number of vertices in the buffer. */
	uint32_t GetVertexCount() const { return m_vertexCount; }

	/** @brief Byte stride between consecutive vertices. */
	uint32_t GetStride() const { return m_stride; }

	/** @brief Underlying Vulkan buffer handle for binding. */
	vk::Buffer buffer() const { return m_buffer.buffer(); }

private:
	VulkanBuffer m_buffer;
	uint32_t m_vertexCount;
	uint32_t m_stride;
};

} // namespace neurus
