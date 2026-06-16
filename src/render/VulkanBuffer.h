#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

/**
 * @brief RAII wrapper around vk::raii::Buffer + vk::raii::DeviceMemory
 *        with staging upload support.
 *
 * Creates a GPU buffer with the specified size, usage, and memory properties.
 * The Upload() method uses a host-visible staging buffer + vkCmdCopyBuffer
 * to transfer host data to the device-local allocation efficiently.
 *
 * @note Buffer usage flags must include VK_BUFFER_USAGE_TRANSFER_DST_BIT
 *       for Upload() to work.
 *
 * Usage:
 *   VulkanBuffer vbo(device, physicalDevice, queue, qfi,
 *                    1024, vk::BufferUsageFlagBits::eVertexBuffer |
 *                          vk::BufferUsageFlagBits::eTransferDst,
 *                    vk::MemoryPropertyFlagBits::eDeviceLocal);
 *   vbo.Upload(vertexData, 1024);
 *   vk::DescriptorBufferInfo descInfo = vbo.GetDescriptorInfo();
 */
class VulkanBuffer
{
public:
	/**
	 * @brief Creates a buffer and allocates bound device memory.
	 *
	 * @param device          Borrowed logical device (outlives this buffer).
	 * @param physicalDevice  Borrowed physical device for memory queries.
	 * @param queue           Borrowed graphics queue for staging upload submits.
	 * @param queueFamilyIndex Queue family index for temp command pool creation.
	 * @param size            Buffer size in bytes.
	 * @param usageFlags      Buffer usage flags (must include eTransferDst for Upload).
	 * @param memoryProperties Memory property flags (e.g., eDeviceLocal).
	 */
	VulkanBuffer(const vk::raii::Device& device,
	             const vk::raii::PhysicalDevice& physicalDevice,
	             vk::Queue queue,
	             uint32_t queueFamilyIndex,
	             vk::DeviceSize size,
	             vk::BufferUsageFlags usageFlags,
	             vk::MemoryPropertyFlags memoryProperties);
	~VulkanBuffer();

	// Non-copyable — owns GPU resources
	VulkanBuffer(const VulkanBuffer&) = delete;
	VulkanBuffer& operator=(const VulkanBuffer&) = delete;

	// Movable
	VulkanBuffer(VulkanBuffer&&) noexcept;
	VulkanBuffer& operator=(VulkanBuffer&&) noexcept;

	/**
	 * @brief Uploads host data using a staging buffer.
	 *
	 * Creates a temporary host-visible staging buffer, copies the source data,
	 * records a vkCmdCopyBuffer into a one-shot command buffer, submits it to
	 * the queue, and waits for completion. After the call returns, the buffer
	 * data is visible to subsequent GPU operations.
	 *
	 * @param data Pointer to source data on the host.
	 * @param size Number of bytes to upload (must not exceed buffer size).
	 */
	void Upload(const void* data, vk::DeviceSize size);

	/**
	 * @brief Maps buffer memory for direct host access.
	 *
	 * Only valid for host-visible memory (HOST_VISIBLE | HOST_COHERENT).
	 * Caller must ensure no GPU operations are in-flight.
	 *
	 * @return Mapped pointer to the buffer memory.
	 */
	void* Map();

	/**
	 * @brief Unmaps previously mapped buffer memory.
	 *
	 * After unmap, the mapped pointer becomes invalid. GPU operations may
	 * subsequently access the buffer.
	 */
	void Unmap();

	/**
	 * @brief Returns a VkDescriptorBufferInfo for descriptor writes.
	 *
	 * @param offset Byte offset into the buffer (default: 0).
	 * @param range  Byte range, or VK_WHOLE_SIZE for the full buffer (default).
	 * @return Descriptor buffer info referencing this buffer.
	 */
	vk::DescriptorBufferInfo GetDescriptorInfo(
		vk::DeviceSize offset = 0,
		vk::DeviceSize range = VK_WHOLE_SIZE) const;

	/** @brief Returns the underlying vk::Buffer handle. */
	vk::Buffer buffer() const { return m_bufferRaw; }

	/** @brief Returns the buffer size in bytes. */
	vk::DeviceSize size() const { return m_size; }

private:
	const vk::raii::Device* m_device;
	const vk::raii::PhysicalDevice* m_physicalDevice;
	vk::Queue m_queue;
	uint32_t m_queueFamilyIndex;
	std::unique_ptr<vk::raii::Buffer> m_buffer;
	std::unique_ptr<vk::raii::DeviceMemory> m_memory;
	vk::Buffer m_bufferRaw;   // cached raw handle for GetDescriptorInfo / buffer()
	vk::DeviceSize m_size;
	vk::BufferUsageFlags m_usageFlags;
	vk::MemoryPropertyFlags m_memoryProperties;
};

} // namespace neurus
