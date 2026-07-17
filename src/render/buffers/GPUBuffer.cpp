#include "GPUBuffer.h"

#include "core/Log.h"

#include <stdexcept>
#include <cstring>

namespace neurus {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

GPUBuffer::GPUBuffer(const vk::raii::Device& device,
                     const vk::raii::PhysicalDevice& physicalDevice,
                     vk::Queue queue,
                     uint32_t queueFamilyIndex,
                     vk::DeviceSize size,
                     vk::BufferUsageFlags usageFlags,
                     const char* debugName)
	: b_queue(queue)
	, b_queueFamilyIndex(queueFamilyIndex)
{
	createBuffer(device, physicalDevice,
	             size,
	             usageFlags | vk::BufferUsageFlagBits::eTransferDst,
	             vk::MemoryPropertyFlagBits::eDeviceLocal,
	             debugName);
	b_staging = std::make_unique<StagingBuffer>(*b_device,
		                                        *b_physicalDevice,
		                                        b_queue,
		                                        b_queueFamilyIndex,
		                                        b_size,
		                                        b_debugName.empty() ? nullptr
		                                                            : (b_debugName + "_Staging").c_str());

	NEURUS_LOG("[GPUBuffer::Map] created staging buffer, size=" << b_size);
}

// ---------------------------------------------------------------------------
// Upload (Map + memcpy + Unmap)
// ---------------------------------------------------------------------------

void GPUBuffer::Upload(const void* data, vk::DeviceSize size)
{
	if (size > b_size)
	{
		throw std::runtime_error("GPUBuffer::Upload: data size exceeds buffer capacity.");
	}

	void* mapped = Map();
	std::memcpy(mapped, data, static_cast<size_t>(size));
	Unmap();
}

// ---------------------------------------------------------------------------
// Map — lazily creates staging buffer, returns writable pointer
// ---------------------------------------------------------------------------

void* GPUBuffer::Map()
{
	return b_staging->Map();
}

// ---------------------------------------------------------------------------
// Unmap — copies staging buffer to device-local via vkCmdCopyBuffer
// ---------------------------------------------------------------------------

void GPUBuffer::Unmap()
{
	b_staging->Unmap();

	// --- Create transient command pool and one-shot command buffer ---
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
	                                 b_queueFamilyIndex);
	vk::raii::CommandPool cmdPool(*b_device, poolCI);

	vk::CommandBufferAllocateInfo allocInfo(*cmdPool,
	                                        vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(*b_device, allocInfo);

	// --- Record copy ---
	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	cmdBufs[0].begin(beginInfo);

	vk::BufferCopy copyRegion(0, 0, b_size);
	cmdBufs[0].copyBuffer(b_staging->buffer(), this->buffer(), copyRegion);

	// Memory barrier to ensure transfer writes are visible to all
	// subsequent GPU operations on this buffer.
	vk::MemoryBarrier barrier(
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite);
	cmdBufs[0].pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eAllCommands,
		{},
		{barrier}, {}, {});

	cmdBufs[0].end();

	// --- Submit and wait for completion ---
	vk::SubmitInfo submitInfo({}, {}, *cmdBufs[0]);
	b_queue.submit(submitInfo);
	b_queue.waitIdle();
}

} // namespace neurus
