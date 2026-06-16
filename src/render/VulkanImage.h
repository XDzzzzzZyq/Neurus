#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <type_traits>

namespace neurus {

/**
 * @brief RAII wrapper around vk::raii::Image + vk::raii::DeviceMemory + vk::raii::ImageView.
 *
 * Owns image creation, memory allocation/binding, and view creation.
 * Provides helpers for layout transitions and mipmap generation via blit.
 *
 * Non-copyable, movable.
 */
class VulkanImage
{
public:
	/** @brief Type of image being created (determines view type and create flags). */
	enum class ImageType
	{
		e2D,            ///< Standard 2D image, arrayLayers = 1
		eCube,          ///< Cubemap: arrayLayers = 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
		eDepthStencil   ///< Depth / stencil attachment; aspect determined by format
	};

	/**
	 * @brief Creates the image, allocates device memory, binds it, and creates a default image view.
	 *
	 * @param device        Logical device.
	 * @param physicalDevice Physical device (for memory type queries and format support).
	 * @param extent        Image dimensions.
	 * @param format        Pixel format.
	 * @param usage         Usage flags (e.g. Sampled | TransferDst).
	 * @param mipLevels     Number of mip levels (1 = no mip chain).
	 * @param imageType     Image type (2D, Cube, Depth/Stencil).
	 */
	VulkanImage(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice,
	            vk::Extent2D extent,
	            vk::Format format,
	            vk::ImageUsageFlags usage,
	            uint32_t mipLevels = 1,
	            ImageType imageType = ImageType::e2D);

	~VulkanImage() = default;

	// --- Non-copyable, movable ---
	VulkanImage(const VulkanImage&) = delete;
	VulkanImage& operator=(const VulkanImage&) = delete;
	VulkanImage(VulkanImage&&) noexcept = default;
	VulkanImage& operator=(VulkanImage&&) noexcept = default;

	// --- Layout transitions ---

	/**
	 * @brief Inserts a pipeline barrier to transition image layout.
	 *
	 * @param cmdBuf         Command buffer to record the barrier into.
	 * @param oldLayout      Current layout (use eUndefined for first transition).
	 * @param newLayout      Target layout.
	 * @param baseMipLevel   First mip level to transition.
	 * @param levelCount     Number of mip levels to transition (default: all remaining).
	 * @param baseArrayLayer First array layer to transition.
	 * @param layerCount     Number of array layers to transition (default: all remaining).
	 */
	void TransitionLayout(const vk::raii::CommandBuffer& cmdBuf,
	                      vk::ImageLayout oldLayout,
	                      vk::ImageLayout newLayout,
	                      uint32_t baseMipLevel = 0,
	                      uint32_t levelCount = VK_REMAINING_MIP_LEVELS,
	                      uint32_t baseArrayLayer = 0,
	                      uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS);

	/**
	 * @brief Generates mipmaps via vkCmdBlitImage.
	 *
	 * Assumes level 0 is in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL and
	 * subsequent levels are in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
	 * All levels end in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
	 *
	 * @note The image must have VK_IMAGE_USAGE_TRANSFER_SRC_BIT and
	 *       VK_IMAGE_USAGE_TRANSFER_DST_BIT.
	 *
	 * @param cmdBuf Command buffer to record blit commands into.
	 */
	void GenerateMipmaps(const vk::raii::CommandBuffer& cmdBuf);

	// --- Getters ---

	/** @brief Underlying vk::raii::Image handle. */
	const vk::raii::Image& ImageHandle() const { return m_image; }

	/** @brief Underlying vk::raii::ImageView handle. */
	const vk::raii::ImageView& ImageViewHandle() const { return m_imageView; }

	/** @brief Underlying vk::raii::DeviceMemory handle. */
	const vk::raii::DeviceMemory& DeviceMemoryHandle() const { return m_deviceMemory; }

	/** @brief Image extent. */
	vk::Extent2D Extent() const { return m_extent; }

	/** @brief Image format. */
	vk::Format Format() const { return m_format; }

	/** @brief Number of mip levels. */
	uint32_t MipLevels() const { return m_mipLevels; }

	/** @brief Number of array layers. */
	uint32_t ArrayLayers() const { return m_arrayLayers; }

	/** @brief Image type. */
	ImageType Type() const { return m_imageType; }

private:
	// --- Construction helpers ---
	void createImage(const vk::raii::Device& device,
	                 const vk::raii::PhysicalDevice& physicalDevice);
	void allocateAndBindMemory(const vk::raii::Device& device,
	                           const vk::raii::PhysicalDevice& physicalDevice);
	void createImageView(const vk::raii::Device& device);

	// --- Layout helpers ---
	static vk::AccessFlags AccessFlagsForLayout(vk::ImageLayout layout);
	static vk::PipelineStageFlags PipelineStageForLayout(vk::ImageLayout layout);
	static vk::ImageAspectFlags AspectFromFormat(vk::Format format);
	static uint32_t FindMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
	                               uint32_t typeFilter,
	                               vk::MemoryPropertyFlags properties);

	// --- Resources ---
	vk::raii::Image m_image = nullptr;
	vk::raii::DeviceMemory m_deviceMemory = nullptr;
	vk::raii::ImageView m_imageView = nullptr;

	// --- Metadata ---
	vk::Extent2D m_extent{};
	vk::Format m_format = vk::Format::eUndefined;
	vk::ImageUsageFlags m_usage;
	uint32_t m_mipLevels = 1;
	uint32_t m_arrayLayers = 1;
	ImageType m_imageType = ImageType::e2D;
};

} // namespace neurus
