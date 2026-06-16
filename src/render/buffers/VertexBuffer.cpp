#include "VertexBuffer.h"

#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

VertexBuffer::VertexBuffer(const vk::raii::Device& device,
                           const vk::raii::PhysicalDevice& physicalDevice,
                           vk::Queue queue,
                           uint32_t queueFamilyIndex,
                           const void* data,
                           vk::DeviceSize size,
                           uint32_t stride,
                           uint32_t vertexCount)
	: m_buffer(device,
	           physicalDevice,
	           queue,
	           queueFamilyIndex,
	           size,
	           vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
	           vk::MemoryPropertyFlagBits::eDeviceLocal)
	, m_vertexCount(vertexCount)
	, m_stride(stride)
{
	if (data == nullptr && size > 0)
	{
		throw std::runtime_error("VertexBuffer: data pointer is null but size > 0.");
	}

	if (data != nullptr && size > 0)
	{
		m_buffer.Upload(data, size);
	}
}

} // namespace neurus
