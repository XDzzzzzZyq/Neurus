#pragma once

#include "Buffer.h"

#include <vulkan/vulkan_raii.hpp>

namespace neurus {

/**
 * @brief Host-visible staging buffer for CPU↔GPU data transfer.
 *
 * Allocated with HOST_VISIBLE | HOST_COHERENT memory so the CPU can write
 * directly without explicit flush/invalidate calls. Used as the source
 * for vkCmdCopyBuffer transfers to device-local buffers (via GPUBuffer).
 *
 * @note Stores the queue for use by consumers that submit transfer commands
 *       (e.g., GPUBuffer::Unmap).
 *
 * Usage:
 *   StagingBuffer staging(device, physicalDevice, queue, qfi,
 *                         1024, vk::BufferUsageFlagBits::eTransferSrc, "Staging");
 *   void* ptr = staging.Map();
 *   std::memcpy(ptr, data, size);
 *   staging.Unmap();
 *   // Now use staging.buffer() as source in vkCmdCopyBuffer
 */
class StagingBuffer : public Buffer
{
public:
	/**
	 * @brief Creates a host-visible staging buffer.
	 *
	 * The buffer is created with the given usage flags combined with
	 * eTransferSrc. Memory is allocated as HOST_VISIBLE | HOST_COHERENT.
	 *
	 * @param device           Borrowed logical device (outlives this buffer).
	 * @param physicalDevice   Borrowed physical device for memory queries.
	 * @param queue            Borrowed graphics queue for transfer submits (held for consumers).
	 * @param queueFamilyIndex Queue family index for temp command pool creation.
	 * @param size             Buffer size in bytes.
	 * @param debugName        Optional debug name.
	 */
	StagingBuffer(const vk::raii::Device& device,
	              const vk::raii::PhysicalDevice& physicalDevice,
	              vk::Queue queue,
	              uint32_t queueFamilyIndex,
	              vk::DeviceSize size,
	              const char* debugName = nullptr);

	// Inherits non-copyable / movable from Buffer — no need to re-delete

	/**
	 * @brief Uploads host data directly into host-visible memory.
	 *
	 * Equivalent to Map() + memcpy + Unmap(). No GPU copy is performed —
	 * the data is immediately visible to the GPU if the underlying memory
	 * is host-coherent.
	 *
	 * @param data Pointer to source data on the host.
	 * @param size Number of bytes to copy (must not exceed buffer size).
	 */
	void Upload(const void* data, vk::DeviceSize size) override;

	/**
	 * @brief Maps the host-visible memory for CPU write access.
	 * @return Writable pointer to the buffer memory.
	 */
	void* Map() override;

	/**
	 * @brief Unmaps the host-visible memory.
	 */
	void Unmap() override;

	/** @brief Borrowed queue reference (for consumers submitting transfers). */
	vk::Queue queue() const { return m_queue; }

	/** @brief Queue family index (for consumers creating command pools). */
	uint32_t queueFamilyIndex() const { return m_queueFamilyIndex; }

private:
	vk::Queue m_queue = nullptr;
	uint32_t m_queueFamilyIndex = 0;
};

} // namespace neurus
