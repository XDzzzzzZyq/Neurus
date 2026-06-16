#include "VulkanBuffer.h"

#include "Log.h"

#include <stdexcept>
#include <cstring>

namespace neurus {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * @brief Finds the first memory type index matching the given requirements.
 */
static uint32_t findMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
                               uint32_t memoryTypeBits,
                               vk::MemoryPropertyFlags requiredFlags)
{
	auto memProps = physicalDevice.getMemoryProperties();

	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		if ((memoryTypeBits & (1u << i)) == 0)
		{
			continue;
		}

		if ((memProps.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags)
		{
			return i;
		}
	}

	throw std::runtime_error("VulkanBuffer: No suitable memory type found for the requested properties.");
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

VulkanBuffer::VulkanBuffer(const vk::raii::Device& device,
                           const vk::raii::PhysicalDevice& physicalDevice,
                           vk::Queue queue,
                           uint32_t queueFamilyIndex,
                           vk::DeviceSize size,
                           vk::BufferUsageFlags usageFlags,
                           vk::MemoryPropertyFlags memoryProperties)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
	, m_queue(queue)
	, m_queueFamilyIndex(queueFamilyIndex)
	, m_size(size)
	, m_usageFlags(usageFlags)
	, m_memoryProperties(memoryProperties)
{
	// --- Create buffer ---
	vk::BufferCreateInfo bufferCI({}, m_size, m_usageFlags);
	m_buffer = std::make_unique<vk::raii::Buffer>(*m_device, bufferCI);
	m_bufferRaw = **m_buffer;

	// --- Query memory requirements ---
	auto memReqs = m_buffer->getMemoryRequirements();

	// --- Find memory type ---
	uint32_t memTypeIndex = findMemoryType(*m_physicalDevice,
	                                       memReqs.memoryTypeBits,
	                                       m_memoryProperties);

	// --- Allocate device memory ---
	vk::MemoryAllocateInfo allocInfo(memReqs.size, memTypeIndex);
	m_memory = std::make_unique<vk::raii::DeviceMemory>(*m_device, allocInfo);

	// --- Bind memory to buffer ---
	m_buffer->bindMemory(**m_memory, 0);

	NEURUS_LOG("[VulkanBuffer] size=" << m_size
	          << " usage=" << vk::to_string(m_usageFlags)
	          << " memProps=" << vk::to_string(m_memoryProperties));
}

VulkanBuffer::~VulkanBuffer()
{
	// vk::raii::Buffer and vk::raii::DeviceMemory clean up automatically.
	// The borrowed device must still be alive at this point.
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
	: m_device(other.m_device)
	, m_physicalDevice(other.m_physicalDevice)
	, m_queue(other.m_queue)
	, m_queueFamilyIndex(other.m_queueFamilyIndex)
	, m_buffer(std::move(other.m_buffer))
	, m_memory(std::move(other.m_memory))
	, m_bufferRaw(other.m_bufferRaw)
	, m_size(other.m_size)
	, m_usageFlags(other.m_usageFlags)
	, m_memoryProperties(other.m_memoryProperties)
{
	// Invalidate the moved-from object
	other.m_bufferRaw = vk::Buffer{};
	other.m_size = 0;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
{
	if (this != &other)
	{
		m_device = other.m_device;
		m_physicalDevice = other.m_physicalDevice;
		m_queue = other.m_queue;
		m_queueFamilyIndex = other.m_queueFamilyIndex;
		m_buffer = std::move(other.m_buffer);
		m_memory = std::move(other.m_memory);
		m_bufferRaw = other.m_bufferRaw;
		m_size = other.m_size;
		m_usageFlags = other.m_usageFlags;
		m_memoryProperties = other.m_memoryProperties;

		// Invalidate the moved-from object
		other.m_bufferRaw = vk::Buffer{};
		other.m_size = 0;
	}

	return *this;
}

// ---------------------------------------------------------------------------
// Upload (staging pattern)
// ---------------------------------------------------------------------------

void VulkanBuffer::Upload(const void* data, vk::DeviceSize size)
{
	if (size > m_size)
	{
		throw std::runtime_error("VulkanBuffer::Upload: data size exceeds buffer capacity.");
	}

	if (!(m_usageFlags & vk::BufferUsageFlagBits::eTransferDst))
	{
		throw std::runtime_error(
			"VulkanBuffer::Upload: buffer usage must include VK_BUFFER_USAGE_TRANSFER_DST_BIT.");
	}

	// --- 1. Create staging buffer (host-visible + host-coherent) ---
	vk::BufferCreateInfo stagingCI({}, size,
		vk::BufferUsageFlagBits::eTransferSrc);
	vk::raii::Buffer stagingBuffer(*m_device, stagingCI);

	auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
	uint32_t stagingMemTypeIndex = findMemoryType(*m_physicalDevice,
	                                              stagingMemReqs.memoryTypeBits,
	                                              vk::MemoryPropertyFlagBits::eHostVisible |
	                                              vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::MemoryAllocateInfo stagingAllocInfo(stagingMemReqs.size, stagingMemTypeIndex);
	vk::raii::DeviceMemory stagingMemory(*m_device, stagingAllocInfo);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	// --- 2. Copy source data into staging buffer ---
	void* mapped = stagingMemory.mapMemory(0, size);
	std::memcpy(mapped, data, static_cast<size_t>(size));
	stagingMemory.unmapMemory();

	// --- 3. Create transient command pool and one-shot command buffer ---
	vk::CommandPoolCreateInfo poolCI(
		vk::CommandPoolCreateFlagBits::eTransient,
		m_queueFamilyIndex);
	vk::raii::CommandPool cmdPool(*m_device, poolCI);

	vk::CommandBufferAllocateInfo allocInfo(*cmdPool,
		vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(*m_device, allocInfo);

	// --- 4. Record copy + pipeline barrier ---
	vk::CommandBufferBeginInfo beginInfo(
		vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	cmdBufs[0].begin(beginInfo);

	vk::BufferCopy copyRegion(0, 0, size);
	cmdBufs[0].copyBuffer(*stagingBuffer, **m_buffer, copyRegion);

	// Insert a memory barrier to ensure transfer writes are visible
	// to all subsequent GPU operations on this buffer.
	vk::MemoryBarrier barrier(
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite);
	cmdBufs[0].pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eAllCommands,
		{},
		{barrier}, {}, {});

	cmdBufs[0].end();

	// --- 5. Submit and wait for completion ---
	vk::SubmitInfo submitInfo({}, {}, *cmdBufs[0]);
	m_queue.submit(submitInfo);
	m_queue.waitIdle();

	NEURUS_LOG("[VulkanBuffer::Upload] " << size << " bytes transferred");

	// Staging resources are destroyed via vk::raii when leaving scope.
}

// ---------------------------------------------------------------------------
// Map / Unmap
// ---------------------------------------------------------------------------

void* VulkanBuffer::Map()
{
	return m_memory->mapMemory(0, m_size);
}

void VulkanBuffer::Unmap()
{
	m_memory->unmapMemory();
}

// ---------------------------------------------------------------------------
// GetDescriptorInfo
// ---------------------------------------------------------------------------

vk::DescriptorBufferInfo VulkanBuffer::GetDescriptorInfo(vk::DeviceSize offset,
                                                         vk::DeviceSize range) const
{
	vk::DeviceSize actualRange = (range == VK_WHOLE_SIZE) ? m_size : range;
	return vk::DescriptorBufferInfo(m_bufferRaw, offset, actualRange);
}

} // namespace neurus
