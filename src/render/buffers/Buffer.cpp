#include "Buffer.h"

#include "Log.h"

#include <stdexcept>
#include <cstring>

namespace neurus {

// ---------------------------------------------------------------------------
// Internal helpers (anonymous namespace)
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

	throw std::runtime_error(
		"Buffer: No suitable memory type found for the requested properties.");
}

// ---------------------------------------------------------------------------
// createBuffer — shared construction logic for derived classes
// ---------------------------------------------------------------------------

void Buffer::createBuffer(const vk::raii::Device& device,
                          const vk::raii::PhysicalDevice& physicalDevice,
                          vk::DeviceSize size,
                          vk::BufferUsageFlags usageFlags,
                          vk::MemoryPropertyFlags memoryProperties,
                          const char* debugName)
{
	m_device = &device;
	m_physicalDevice = &physicalDevice;
	m_size = size;
	m_usageFlags = usageFlags;
	m_memoryProperties = memoryProperties;
	m_debugName = debugName ? debugName : "";

	// --- Create buffer ---
	vk::BufferCreateInfo bufferCI({}, m_size, m_usageFlags);
	m_buffer = std::make_unique<vk::raii::Buffer>(*m_device, bufferCI);

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

	// --- Set debug names (Debug builds only) ---
#ifdef _DEBUG
	if (!m_debugName.empty())
	{
		// Name the buffer
		{
			vk::DebugUtilsObjectNameInfoEXT nameInfo(
				vk::ObjectType::eBuffer,
				reinterpret_cast<uint64_t>(static_cast<VkBuffer>(**m_buffer)),
				m_debugName.c_str());
			m_device->setDebugUtilsObjectNameEXT(nameInfo);
		}

		// Name the device memory
		{
			std::string memName = m_debugName + "_Mem";
			vk::DebugUtilsObjectNameInfoEXT nameInfo(
				vk::ObjectType::eDeviceMemory,
				reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(**m_memory)),
				memName.c_str());
			m_device->setDebugUtilsObjectNameEXT(nameInfo);
		}
	}
#endif

	NEURUS_LOG("[Buffer] size=" << m_size
	          << " usage=" << vk::to_string(m_usageFlags)
	          << " memProps=" << vk::to_string(m_memoryProperties)
	          << (!m_debugName.empty() ? " name='" : "")
	          << (!m_debugName.empty() ? m_debugName : "")
	          << (!m_debugName.empty() ? "'" : ""));
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

Buffer::Buffer(Buffer&& other) noexcept
	: m_device(other.m_device)
	, m_physicalDevice(other.m_physicalDevice)
	, m_buffer(std::move(other.m_buffer))
	, m_memory(std::move(other.m_memory))
	, m_size(other.m_size)
	, m_usageFlags(other.m_usageFlags)
	, m_memoryProperties(other.m_memoryProperties)
	, m_debugName(std::move(other.m_debugName))
{
	// Invalidate the moved-from object
	other.m_size = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
	if (this != &other)
	{
		m_device = other.m_device;
		m_physicalDevice = other.m_physicalDevice;
		m_buffer = std::move(other.m_buffer);
		m_memory = std::move(other.m_memory);
		m_size = other.m_size;
		m_usageFlags = other.m_usageFlags;
		m_memoryProperties = other.m_memoryProperties;
		m_debugName = std::move(other.m_debugName);

		// Invalidate the moved-from object
		other.m_size = 0;
	}

	return *this;
}

// ---------------------------------------------------------------------------
// GetDescriptorInfo
// ---------------------------------------------------------------------------

vk::DescriptorBufferInfo Buffer::GetDescriptorInfo(vk::DeviceSize offset,
                                                    vk::DeviceSize range) const
{
	vk::DeviceSize actualRange = (range == VK_WHOLE_SIZE) ? m_size : range;
	return vk::DescriptorBufferInfo(this->buffer(), offset, actualRange);
}

} // namespace neurus
