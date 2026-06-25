#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <string>

namespace neurus {

/**
 * @brief Virtual base class for all GPU buffer types.
 *
 * Encapsulates common buffer lifecycle: creation, memory allocation,
 * binding, debug naming, and descriptor info. Derived classes implement
 * the data transfer strategy (Map/Unmap/Upload) appropriate for their
 * memory domain.
 *
 * @note Non-copyable, movable. The owning device must outlive this buffer.
 *
 * Usage (by derived classes):
 *   class MyBuffer : public Buffer {
 *   public:
 *       MyBuffer(const vk::raii::Device& dev, ...)
 *       {
 *           createBuffer(dev, physicalDevice, size, usage, memProps, name);
 *       }
 *       void Upload(const void*, vk::DeviceSize) override { ... }
 *       void* Map() override { ... }
 *       void Unmap() override { ... }
 *   };
 */
class Buffer
{
public:
	virtual ~Buffer() = default;

	// Non-copyable — owns GPU resources
	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;

	// Movable
	Buffer(Buffer&&) noexcept;
	Buffer& operator=(Buffer&&) noexcept;

	// --- Virtual interface: data transfer strategy ---

	/**
	 * @brief Uploads host data to the buffer.
	 *
	 * The concrete upload mechanism depends on the memory domain:
	 *   - host-visible: Map + memcpy + Unmap (direct write)
	 *   - device-local: staging buffer + vkCmdCopyBuffer + queue submit
	 *
	 * @param data Pointer to source data on the host.
	 * @param size Number of bytes to upload (must not exceed buffer size).
	 */
	virtual void Upload(const void* data, vk::DeviceSize size) = 0;

	/**
	 * @brief Maps buffer memory for direct host access.
	 *
	 * Only valid for host-visible memory. For device-local buffers,
	 * implementations may map a staging buffer and return a writable
	 * proxy pointer.
	 *
	 * @return Mapped pointer to buffer memory.
	 */
	virtual void* Map() = 0;

	/**
	 * @brief Unmaps previously mapped buffer memory.
	 *
	 * After unmap, the mapped pointer becomes invalid. For device-local
	 * buffers, this may trigger a staging-to-GPU transfer.
	 */
	virtual void Unmap() = 0;

	// --- Common non-virtual accessors ---

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
	vk::Buffer buffer() const { return *m_buffer; }

	/** @brief Returns the buffer size in bytes. */
	vk::DeviceSize size() const { return m_size; }

protected:
	/**
	 * @brief Default constructor for move operations (derived classes only).
	 */
	Buffer() = default;

	/**
	 * @brief Creates the underlying VkBuffer, allocates and binds device
	 *        memory, and optionally sets debug names.
	 *
	 * Called by derived-class constructors. On failure, throws
	 * std::runtime_error. On success, m_buffer, m_memory, and m_bufferRaw
	 * are valid and ready for use.
	 *
	 * @param device          Borrowed logical device (must outlive this buffer).
	 * @param physicalDevice  Borrowed physical device for memory type queries.
	 * @param size            Buffer size in bytes.
	 * @param usageFlags      Vulkan buffer usage flags.
	 * @param memoryProperties Desired memory property flags.
	 * @param debugName       Optional debug name (set via VK_EXT_debug_utils in Debug).
	 */
	void createBuffer(const vk::raii::Device& device,
	                  const vk::raii::PhysicalDevice& physicalDevice,
	                  vk::DeviceSize size,
	                  vk::BufferUsageFlags usageFlags,
	                  vk::MemoryPropertyFlags memoryProperties,
	                  const char* debugName = nullptr);

	// --- Borrowed references (must outlive this object) ---
	const vk::raii::Device* m_device = nullptr;
	const vk::raii::PhysicalDevice* m_physicalDevice = nullptr;

	std::unique_ptr<vk::raii::Buffer> m_buffer;
	std::unique_ptr<vk::raii::DeviceMemory> m_memory;
	vk::DeviceSize m_size = 0;
	vk::BufferUsageFlags m_usageFlags;
	vk::MemoryPropertyFlags m_memoryProperties;
	std::string m_debugName;
};

} // namespace neurus
