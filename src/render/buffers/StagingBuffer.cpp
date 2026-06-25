#include "StagingBuffer.h"

#include "Log.h"

#include <stdexcept>
#include <cstring>

namespace neurus {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

StagingBuffer::StagingBuffer(const vk::raii::Device& device,
                             const vk::raii::PhysicalDevice& physicalDevice,
                             vk::Queue queue,
                             uint32_t queueFamilyIndex,
                             vk::DeviceSize size,
                             const char* debugName)
	: m_queue(queue)
	, m_queueFamilyIndex(queueFamilyIndex)
{
	createBuffer(device, physicalDevice,
	             size,
	             vk::BufferUsageFlagBits::eTransferSrc,
	             vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
	             debugName);
}

// ---------------------------------------------------------------------------
// Upload (Map + memcpy + Unmap)
// ---------------------------------------------------------------------------

void StagingBuffer::Upload(const void* data, vk::DeviceSize size)
{
	if (size > m_size)
	{
		throw std::runtime_error("StagingBuffer::Upload: data size exceeds buffer capacity.");
	}

	void* mapped = Map();
	std::memcpy(mapped, data, static_cast<size_t>(size));
	Unmap();

	NEURUS_LOG("[StagingBuffer::Upload] " << size << " bytes written");
}

// ---------------------------------------------------------------------------
// Map / Unmap
// ---------------------------------------------------------------------------

void* StagingBuffer::Map()
{
	return m_memory->mapMemory(0, m_size);
}

void StagingBuffer::Unmap()
{
	m_memory->unmapMemory();
}

} // namespace neurus
