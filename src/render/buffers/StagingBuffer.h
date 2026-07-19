#pragma once

#include "Buffer.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

/**
 * @brief Host-visible staging buffer for CPU↔GPU data transfer.
 *
 * Allocated with HOST_VISIBLE | HOST_COHERENT memory so the CPU can write
 * directly without explicit flush/invalidate calls. Owns a transient command
 * pool and one-shot command buffer — call BeginStaging() to start recording
 * transfer commands, then EndStaging() to submit + wait.
 *
 * Usage (GPU→CPU readback):
 *   StagingBuffer staging(device, pd, queue, qfi, 4, nullptr, eTransferDst);
 *   auto& cmd = staging.BeginStaging();
 *   cmd.copyImageToBuffer(image, ..., staging.buffer(), ...);
 *   staging.EndStaging();
 *   void* data = staging.Map();
 *
 * Usage (CPU→GPU upload):
 *   StagingBuffer staging(device, pd, queue, qfi, size);
 *   staging.Upload(data, size);
 *   auto& cmd = staging.BeginStaging();
 *   cmd.copyBufferToImage(staging.buffer(), image, ...);
 *   staging.EndStaging();
 */
class StagingBuffer : public Buffer
{
public:
	/**
	 * @brief Creates a host-visible staging buffer.
	 *
	 * The buffer is created with @p extraUsage OR-ed with the default usage
	 * (eTransferSrc). Pass eTransferDst as @p extraUsage for GPU→CPU readback.
	 * Memory is allocated as HOST_VISIBLE | HOST_COHERENT.
	 *
	 * @param device           Borrowed logical device (outlives this buffer).
	 * @param physicalDevice   Borrowed physical device for memory queries.
	 * @param size             Buffer size in bytes.
	 * @param debugName        Optional debug name.
	 * @param extraUsage       Additional buffer usage flags (OR-ed with eTransferSrc).
	 */
	StagingBuffer(const vk::raii::Device& device,
	              const vk::raii::PhysicalDevice& physicalDevice,
	              vk::DeviceSize size,
	              const char* debugName = nullptr,
	              vk::BufferUsageFlags extraUsage = {});

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
	 * @brief Maps the host-visible memory for CPU read/write access.
	 * @return Writable pointer to the buffer memory.
	 */
	void* Map() override;

	/**
	 * @brief Unmaps the host-visible memory.
	 */
	void Unmap() override;

	// --- Transient command buffer lifecycle ---

	/**
	 * @brief Begins (or re-begins) the transient one-shot command buffer.
	 *
	 * Lazily creates the command pool on first call. Subsequent calls reset
	 * the pool and re-begin. The returned command buffer is ready for recording
	 * transfer commands that reference this staging buffer.
	 *
	 * @param queueFamilyIndex Queue family index for the transient command pool.
	 * @return Reference to the begun one-shot command buffer.
	 */
	vk::raii::CommandBuffer& BeginStaging(uint32_t queueFamilyIndex);

	/**
	 * @brief Ends the command buffer, submits to @p queue, and waits idle.
	 *
	 * Must be paired with a prior BeginStaging() call. Drains the GPU
	 * pipeline — after this returns, the staging buffer contents are valid
	 * for CPU read (GPU→CPU) or the destination resource has been written
	 * (CPU→GPU).
	 *
	 * @param queue Queue to submit the transfer command buffer to.
	 */
	void EndStaging(vk::Queue queue);

private:
	std::unique_ptr<vk::raii::CommandPool> b_cmdPool;
	std::unique_ptr<vk::raii::CommandBuffers> b_cmdBufs;
};

} // namespace neurus
