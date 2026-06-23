#pragma once

#include "asset/ImageData.h"

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
 * Holds an ImageData member that records the CPU-side pixel data used to
 * create the image (analogous to Mesh::o_mesh holding MeshData).
 *
 * Non-copyable, movable.
 */
class Image
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
	 * @param debugName     Optional debug name for the image (set via VK_EXT_debug_utils in Debug builds).
	 *                      The string is NOT retained; it is used immediately and may be a temporary.
	 * @param imageData     Optional CPU-side pixel data (stored for reference, not uploaded).
	 */
	Image(const vk::raii::Device& device,
	      const vk::raii::PhysicalDevice& physicalDevice,
	      vk::Extent2D extent,
	      vk::Format format,
	      vk::ImageUsageFlags usage,
	      uint32_t mipLevels = 1,
	      ImageType imageType = ImageType::e2D,
	      const char* debugName = nullptr,
	      const ImageData& imageData = {});

	~Image() = default;

	// --- Non-copyable, movable ---
	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;
	Image(Image&&) noexcept = default;
	Image& operator=(Image&&) noexcept = default;

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

	// --- GPU readback ---

	/**
	 * @brief Uploads raw pixel data from CPU to this GPU image via staging buffer.
	 *
	 * Creates a host-visible staging buffer, copies the pixel data to it,
	 * records a vkCmdCopyBufferToImage command, submits, and waits for
	 * completion.  The image is transitioned from UNDEFINED → TRANSFER_DST_OPTIMAL
	 * before the copy and left in SHADER_READ_ONLY_OPTIMAL after completion.
	 *
	 * @note The image MUST have VK_IMAGE_USAGE_TRANSFER_DST_BIT.
	 * @param device          Logical device.
	 * @param physicalDevice  Physical device for memory queries.
	 * @param queue           Queue for staging upload submits.
	 * @param queueFamilyIndex Queue family index for temp command pool.
	 * @param pixelData       Pointer to raw pixel bytes.
	 * @param dataSize        Total byte count (must match extent × bytesPerPixel).
	 */
	void UploadPixelData(const vk::raii::Device& device,
	                     const vk::raii::PhysicalDevice& physicalDevice,
	                     vk::Queue queue,
	                     uint32_t queueFamilyIndex,
	                     const void* pixelData,
	                     size_t dataSize);

	/**
	 * @brief Reads image data from the GPU into a host‑side byte vector.
	 *
	 * Creates a transient command buffer, records vkCmdCopyImageToBuffer
	 * from this image into a staging buffer, submits, waits for completion,
	 * and returns the raw pixel data.  The image must be in a TRANSFER_SRC
	 * compatible layout (typically eTransferSrcOptimal).
	 *
	 * @param currentLayout    Current layout of the image (must be transfer‑src compatible).
	 * @return Raw pixel data (size = width × height × bytesPerPixel).
	 */
	std::vector<uint8_t> ReadImageToBuffer(const vk::raii::Device& device,
	                                       const vk::raii::PhysicalDevice& physicalDevice,
	                                       vk::Queue queue,
	                                       uint32_t queueFamilyIndex,
	                                       vk::ImageLayout currentLayout) const;

	/**
	 * @brief Static overload for raw VkImage handles (swapchain, etc.).
	 *
	 * Same behaviour as the member version but accepts an explicit image
	 * handle, format, and extent.  Useful for swapchain screenshots where
	 * no Image wrapper exists.
	 */
	static std::vector<uint8_t> ReadImageToBuffer(const vk::raii::Device& device,
	                                               const vk::raii::PhysicalDevice& physicalDevice,
	                                               vk::Queue queue,
	                                               uint32_t queueFamilyIndex,
	                                               vk::Image image,
	                                               vk::Format format,
	                                               vk::Extent2D extent,
	                                               vk::ImageLayout currentLayout);

	/**
	 * @brief Loads an image from file (PNG, HDR, BMP, JPG, etc.) and uploads to GPU.
	 *
	 * Uses ImageData::LoadFromPath() for CPU-side format detection and loading,
	 * then creates an Image with the appropriate format and uploads pixel data.
	 * HDR files use R32G32B32A32_SFLOAT; LDR files use R8G8B8A8_SRGB.
	 *
	 * @param device           Logical device.
	 * @param physicalDevice   Physical device for memory allocation.
	 * @param queue            Queue for staging upload.
	 * @param queueFamilyIndex Queue family index.
	 * @param path             File path to the image.
	 * @param debugName        Optional debug name for the image.
	 * @return Unique pointer to GPU Image, or nullptr on failure.
	 */
	static std::unique_ptr<Image> LoadFromPath(const vk::raii::Device& device,
	                                           const vk::raii::PhysicalDevice& physicalDevice,
	                                           vk::Queue queue,
	                                           uint32_t queueFamilyIndex,
	                                           const std::string& path,
	                                           const char* debugName = "Equirect_LoadFromPath");

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

	/** @brief CPU-side pixel data used to create this image (may be default-constructed). */
	const ImageData& GetImageData() const { return m_imageData; }

	/**
	 * @brief Returns the Vulkan image aspect flags for a given format.
	 *
	 * Maps D16_UNORM / D32_SFLOAT to eDepth, D24_UNORM_S8_UINT / D32_SFLOAT_S8_UINT
	 * to eDepth | eStencil, and all other formats to eColor.
	 */
	static vk::ImageAspectFlags AspectFromFormat(vk::Format format);

	// --- Layout state ---

	/**
	 * @brief Returns the tracked current layout of the image.
	 *
	 * Updated by TransitionLayout() and SetCurrentLayout().  Initialised to
	 * eUndefined.  This is a CPU-side convenience; the real layout is
	 * maintained by Vulkan and the validation layer.
	 */
	vk::ImageLayout CurrentLayout() const { return m_currentLayout; }

	/**
	 * @brief Sets the tracked current layout to the given value.
	 *
	 * Call after recording barriers that do NOT go through TransitionLayout()
	 * (e.g. pipelineBarrier2 or dynamic-rendering barriers in user code).
	 */
	void SetCurrentLayout(vk::ImageLayout layout) { m_currentLayout = layout; }

private:
	// --- Construction helpers ---
	void createImage(const vk::raii::Device& device,
	                 const vk::raii::PhysicalDevice& physicalDevice);
	void allocateAndBindMemory(const vk::raii::Device& device,
	                           const vk::raii::PhysicalDevice& physicalDevice);
	void createImageView(const vk::raii::Device& device, const char* debugName);
	void createFaceViews(const vk::raii::Device& device);
	void createMultiviewView(const vk::raii::Device& device);

	// --- Layout helpers ---
	static vk::AccessFlags AccessFlagsForLayout(vk::ImageLayout layout);
	static vk::PipelineStageFlags PipelineStageForLayout(vk::ImageLayout layout);
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

	// --- CPU-side data (optional, stored for reference) ---
	ImageData m_imageData;

	// --- Tracked current layout (CPU-side for convenience) ---
	vk::ImageLayout m_currentLayout = vk::ImageLayout::eUndefined;
};

} // namespace neurus
