#include "IndexBuffer.h"

#include "Log.h"

#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

IndexBuffer::IndexBuffer(const vk::raii::Device& device,
                         const vk::raii::PhysicalDevice& physicalDevice,
                         vk::Queue queue,
                         uint32_t queueFamilyIndex,
                         const uint32_t* data,
                         vk::DeviceSize size,
                         uint32_t indexCount,
                         const char* debugName)
	: GPUBuffer(device,
	            physicalDevice,
	            queue,
	            queueFamilyIndex,
	            size,
	            vk::BufferUsageFlagBits::eIndexBuffer,
	            debugName)
	, m_indexCount(indexCount)
{
	if (data == nullptr && size > 0)
	{
		throw std::runtime_error("IndexBuffer: data pointer is null but size > 0.");
	}

	if (data != nullptr && size > 0)
	{
		Upload(data, size);
	}

	NEURUS_LOG("[IndexBuffer] " << indexCount << " indices, size=" << size << " bytes"
	          << (debugName ? " name='" : "")
	          << (debugName ? debugName : "")
	          << (debugName ? "'" : ""));
}

} // namespace neurus
