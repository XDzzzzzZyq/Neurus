#pragma once

#include "asset/ImageData.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <type_traits>
#include <vector>

namespace neurus {

// Forward declaration for friend access
class Barrier;

/**
 * @brief High-level image state used by Barrier for layout transitions.
 *
 * Maps to the appropriate vk::ImageLayout, vk::PipelineStageFlags2,
 * and vk::AccessFlags2 via Barrier::ToVulkanImageState().
 */
enum class ImageState
{
	Undefined,       ///< Initial state, no valid data

	TransferSrc,     ///< Source for transfer (copy/blit) operations
	TransferDst,     ///< Destination for transfer (copy/blit) operations

	ColorAttachment, ///< Color render target
	DepthAttachment, ///< Depth/stencil render target

	ColorShaderRead,  ///< Sampled/input attachment read for color images
	DepthShaderRead,  ///< Sampled/input attachment read for depth images
	ShaderWrite,      ///< Storage image write in compute shaders

	Present,         ///< Ready for presentation
};

/**
 * @brief RAII wrapper around vk::raii::Image + vk::raii::DeviceMemory + vk::raii::ImageView.
 *
 * Owns image creation, memory allocation/binding, and view creation.
 * Tracks current logical state via ImageState for use with Barrier::Transition().
 *
 * ImageData is used only during construction — no CPU-side data is retained.
 *
 * Non-copyable, movable.
 */
class Image
{
	friend class Barrier;

public:
	/** @brief Type of image being created (determines view type and create flags). */
	enum class ImageType
	{
		e2D,            ///< Standard 2D image, arrayLayers = 1
		eCube,          ///< Cubemap: arrayLayers = 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
		eDepthStencil   ///< Depth / stencil attachment; aspect determined by format
	};

	/**
	 * @brief Creates a GPU image from scratch (no pixel data upload).
	 *
	 * The image starts in ImageState::Undefined.  Use Barrier::Transition() to
	 * move it to a usable state, or call the ImageData constructor for automatic
	 * upload.
	 *
	 * @param device         Logical device.
	 * @param physicalDevice Physical device (for memory type queries and format support).
	 * @param extent         Image dimensions.
	 * @param format         Pixel format.
	 * @param usage          Usage flags (e.g. Sampled | TransferDst).
	 * @param mipLevels      Number of mip levels (1 = no mip chain).
	 * @param imageType      Image type (2D, Cube, Depth/Stencil).
	 * @param debugName      Optional debug name for the image.
	 */
	Image(const vk::raii::Device& device,
	      const vk::raii::PhysicalDevice& physicalDevice,
	      vk::Extent2D extent,
	      vk::Format format,
	      vk::ImageUsageFlags usage,
	      uint32_t mipLevels = 1,
	      ImageType imageType = ImageType::e2D,
	      const char* debugName = nullptr);

	/**
	 * @brief Creates a GPU image from CPU-side ImageData (loads from file and uploads).
	 *
	 * Uses ImageData for dimensions, format, and pixel content.  Automatically
	 * creates the image, uploads pixel data via staging buffer, and leaves the
	 * image in ImageState::ColorShaderRead.
	 *
	 * @param device          Logical device.
	 * @param physicalDevice  Physical device for memory allocation.
	 * @param queue           Queue for staging upload.
	 * @param queueFamilyIndex Queue family index for transient command pools.
	 * @param imageData       CPU-side image data (moved in, must be valid).
	 * @param debugName       Optional debug name for the image.
	 * @return A unique pointer to the GPU Image, or nullptr if imageData is invalid.
	 */
	static std::unique_ptr<Image> FromImageData(const vk::raii::Device& device,
	                                            const vk::raii::PhysicalDevice& physicalDevice,
	                                            vk::Queue queue,
	                                            uint32_t queueFamilyIndex,
	                                            ImageData& imageData,
	                                            const char* debugName = "Image");

	~Image() = default;

	// --- Non-copyable, movable ---
	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;
	Image(Image&&) noexcept = default;
	Image& operator=(Image&&) noexcept = default;

	/**
	 * @brief Generates mipmaps via vkCmdBlitImage.
	 *
	 * The image must be in ImageState::TransferDst (all levels).  Level 0 is
	 * blitted to level 1, then 1→2, etc.  All levels end in ImageState::ColorShaderRead.
	 *
	 * @note The image must have VK_IMAGE_USAGE_TRANSFER_SRC_BIT and
	 *       VK_IMAGE_USAGE_TRANSFER_DST_BIT.
	 *
	 * @param cmdBuf Command buffer to record blit commands into.
	 */
	void GenerateMipmaps(const vk::raii::CommandBuffer& cmdBuf);

	/**
	 * @brief Reads image data from the GPU back into an ImageData.
	 *
	 * The image must be in ImageState::TransferSrc.  Creates a transient
	 * command buffer, records vkCmdCopyImageToBuffer into a staging buffer,
	 * submits, waits, and returns the pixel data as ImageData.
	 *
	 * @return ImageData with the pixel content, or default-constructed on failure.
	 */
	ImageData ReadImageData(const vk::raii::Device& device,
	                        const vk::raii::PhysicalDevice& physicalDevice,
	                        vk::Queue queue,
	                        uint32_t queueFamilyIndex) const;

	// --- Getters ---

	/** @brief Underlying vk::raii::Image handle. */
	const vk::raii::Image& ImageHandle() const { return m_image; }

	/** @brief Underlying vk::raii::ImageView handle (default view). */
	const vk::raii::ImageView& ImageViewHandle() const { return m_imageView; }

	/**
	 * @brief Per-layer 2D view for rendering to a specific cubemap face.
	 * @param faceIdx Face index in [0,5].
	 * @note Valid only for ImageType::eCube.
	 */
	const vk::raii::ImageView& FaceView(uint32_t faceIdx) const;

	/**
	 * @brief 2D_ARRAY view covering all 6 layers for multiview rendering.
	 * @note Valid only for ImageType::eCube.
	 */
	const vk::raii::ImageView& ArrayView() const;

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

	/** @brief Current logical image state (updated by Barrier::Transition). */
	ImageState State() const { return m_state; }

	// --- Subresource range helpers ---

	/** @brief Subresource range covering the entire image (all mips, all layers). */
	vk::ImageSubresourceRange AllSubresources() const;

	/** @brief Subresource range for a single mip level (all layers). */
	vk::ImageSubresourceRange Mip(uint32_t level) const;

	/** @brief Subresource range for a contiguous range of mip levels (all layers). */
	vk::ImageSubresourceRange Mips(uint32_t base, uint32_t count) const;

	/** @brief Subresource range for a single array layer (all mips). */
	vk::ImageSubresourceRange Layer(uint32_t layer) const;

	/** @brief Subresource range for a contiguous range of array layers (all mips). */
	vk::ImageSubresourceRange Layers(uint32_t base, uint32_t count) const;

private:
	// --- Construction helpers ---
	void createImage(const vk::raii::Device& device,
	                 const vk::raii::PhysicalDevice& physicalDevice);
	void allocateAndBindMemory(const vk::raii::Device& device,
	                           const vk::raii::PhysicalDevice& physicalDevice);
	void createImageView(const vk::raii::Device& device, const char* debugName);
	void createFaceViews(const vk::raii::Device& device);
	void createMultiviewView(const vk::raii::Device& device);

	/**
	 * @brief Uploads ImageData to this GPU image via staging buffer.
	 *
	 * Creates a host-visible staging buffer, copies the pixel data to it,
	 * records vkCmdCopyBufferToImage, submits, and waits.
	 * Transitions: Undefined → TransferDst → (copy) → ColorShaderRead.
	 */
	void UploadImageData(const vk::raii::Device& device,
	                     const vk::raii::PhysicalDevice& physicalDevice,
	                     vk::Queue queue,
	                     uint32_t queueFamilyIndex,
	                     const ImageData& imageData);

	/**
	 * @brief Returns the Vulkan image aspect flags for a given format.
	 */
	static vk::ImageAspectFlags AspectFromFormat(vk::Format format);

	/** @brief Memory type selection helper. */
	static uint32_t FindMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
	                               uint32_t typeFilter,
	                               vk::MemoryPropertyFlags properties);

	// --- Resources ---
	vk::raii::Image m_image = nullptr;
	vk::raii::DeviceMemory m_deviceMemory = nullptr;
	vk::raii::ImageView m_imageView = nullptr;

	// --- Cube-only views ---
	std::vector<vk::raii::ImageView> m_faceViews;
	vk::raii::ImageView m_multiviewView = nullptr;

	// --- Metadata ---
	vk::Extent2D m_extent{};
	vk::Format m_format = vk::Format::eUndefined;
	vk::ImageUsageFlags m_usage;
	uint32_t m_mipLevels = 1;
	uint32_t m_arrayLayers = 1;
	ImageType m_imageType = ImageType::e2D;

	// --- State tracking ---
	ImageState m_state = ImageState::Undefined;
};

} // namespace neurus
