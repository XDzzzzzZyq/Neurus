#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace neurus {

// --- Forward declarations ---
class VulkanImage;
class AttachmentManager;

/**
 * @brief Static utility for capturing Vulkan images to PNG files.
 *
 * Uses vkCmdCopyImageToBuffer to read GPU image data into a host-visible
 * staging buffer, then writes the result via stbi_write_png.
 * All operations are blocking (waits for queue idle).
 *
 * Supports:
 * - Swapchain images (B8G8R8A8_SRGB → RGBA8 PNG with BGR swizzle)
 * - G-Buffer attachments (RGBA8, BGRA8, RGBA16F → u8 PNG)
 * - Batch capture of all attachments via CaptureAllAttachments
 *
 * The constructor is deleted - this is a purely static utility class.
 */
class Screenshot
{
public:
	Screenshot() = delete;

	// Non-copyable, non-movable - purely static utility
	Screenshot(const Screenshot&) = delete;
	Screenshot& operator=(const Screenshot&) = delete;
	Screenshot(Screenshot&&) = delete;
	Screenshot& operator=(Screenshot&&) = delete;

	// -----------------------------------------------------------------------
	// Utility helpers
	// -----------------------------------------------------------------------

	/**
	 * @brief Generates a timestamp string: "YYYYMMDD_HHMMSS".
	 * @param prefix The string before the timestamp.
	 * @param suffix The string after the timestamp (e.g., ".png").
	 * @return "{prefix}_YYYYMMDD_HHMMSS{suffix}"
	 */
	static std::string timestampedFilename(const std::string& prefix,
	                                       const std::string& suffix);

	// -----------------------------------------------------------------------
	// Capture methods
	// -----------------------------------------------------------------------

	/**
	 * @brief Captures a swapchain image to a PNG file.
	 *
	 * Transitions the image from PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL,
	 * copies to a host-visible staging buffer, writes PNG, then transitions
	 * back to PRESENT_SRC_KHR.
	 *
	 * Handles BGR→RGB channel swizzle for B8G8R8A8_SRGB / B8G8R8A8_UNORM
	 * swapchain formats.
	 *
	 * @param device           Logical device (borrowed).
	 * @param physicalDevice   Physical device for memory queries.
	 * @param queue            Graphics queue for one-shot submit + waitIdle.
	 * @param queueFamilyIndex Queue family index for transient command pool.
	 * @param image            Raw VkImage handle of the swapchain image.
	 * @param format           Image format (typically B8G8R8A8_SRGB).
	 * @param extent           Image dimensions in pixels.
	 * @param path             Output PNG file path (parent directories created).
	 * @return true on success, false on failure.
	 */
	static bool CaptureSwapchain(const vk::raii::Device& device,
	                             const vk::raii::PhysicalDevice& physicalDevice,
	                             vk::Queue queue,
	                             uint32_t queueFamilyIndex,
	                             vk::Image image,
	                             vk::Format format,
	                             vk::Extent2D extent,
	                             const std::string& path);

	/**
	 * @brief Captures a VulkanImage attachment to a PNG file.
	 *
	 * Transitions the image to TRANSFER_SRC_OPTIMAL, copies to a staging buffer,
	 * writes PNG, then transitions back to the original layout.
	 *
	 * Format handling:
	 * - RGBA8 / BGRA8: direct pixel copy (BGRA is swizzled to RGBA).
	 * - RGBA16F / RGBA16_SNORM: half-float values are converted to u8.
	 *   Pass remapSigned=true to remap [-1,1]→[0,1] (for normal maps).
	 * - R8_UNORM: expanded to single-channel grayscale PNG.
	 *
	 * @param device           Logical device (borrowed).
	 * @param physicalDevice   Physical device for memory queries.
	 * @param queue            Graphics queue for one-shot submit + waitIdle.
	 * @param queueFamilyIndex Queue family index for transient command pool.
	 * @param vulkanImage      The VulkanImage to capture (non-const: layout is transitioned).
	 * @param path             Output PNG file path (parent directories created).
	 * @param remapSigned      If true, remap signed half-float data to [0,1] range.
	 * @return true on success, false on failure.
	 */
	static bool CaptureAttachment(const vk::raii::Device& device,
	                              const vk::raii::PhysicalDevice& physicalDevice,
	                              vk::Queue queue,
	                              uint32_t queueFamilyIndex,
	                              VulkanImage& vulkanImage,
	                              const std::string& path,
	                              bool remapSigned = false);

	/**
	 * @brief Captures all G-Buffer and post-FX attachments to timestamped PNG files.
	 *
	 * Iterates all known AttachmentName values, checks HasAttachment(),
	 * and calls CaptureAttachment() for each. Files are named:
	 *   {prefix}_{AttachmentName}_YYYYMMDD_HHMMSS.png
	 *
	 * Skips depth attachments (D32_SFLOAT) as they cannot be read back as color.
	 *
	 * @param device            Logical device (borrowed).
	 * @param physicalDevice    Physical device for memory queries.
	 * @param queue             Graphics queue for one-shot submit + waitIdle.
	 * @param queueFamilyIndex  Queue family index for transient command pool.
	 * @param attachmentManager The attachment manager holding all G-Buffer attachments.
	 * @param prefix            Filename prefix (e.g., "screenshots/capture").
	 *                          Parent directories are created if needed.
	 * @return Number of attachments successfully captured.
	 */
	static int CaptureAllAttachments(const vk::raii::Device& device,
	                                 const vk::raii::PhysicalDevice& physicalDevice,
	                                 vk::Queue queue,
	                                 uint32_t queueFamilyIndex,
	                                 AttachmentManager& attachmentManager,
	                                 const std::string& prefix);

private:
	// -----------------------------------------------------------------------
	// Internal helpers
	// -----------------------------------------------------------------------

	/** @brief Creates parent directories for the given file path. */
	static void ensureDirectory(const std::string& filePath);

	/**
	 * @brief Reads image data from the GPU into a host-side byte vector.
	 *
	 * Creates a transient command pool + one-shot command buffer, records
	 * vkCmdCopyImageToBuffer, submits, and waits for completion.
	 *
	 * @param device           Logical device.
	 * @param physicalDevice   Physical device for memory allocation.
	 * @param queue            Queue for submit + waitIdle.
	 * @param queueFamilyIndex Queue family for transient command pool.
	 * @param image            Raw VkImage handle to read from.
	 * @param format           Image format (determines pixel byte size).
	 * @param extent           Image dimensions.
	 * @param currentLayout    Current image layout (must be TRANSFER_SRC or compatible).
	 * @return Raw pixel data (size = width * height * pixelBytes).
	 */
	static std::vector<uint8_t> readImageToBuffer(const vk::raii::Device& device,
	                                               const vk::raii::PhysicalDevice& physicalDevice,
	                                               vk::Queue queue,
	                                               uint32_t queueFamilyIndex,
	                                               vk::Image image,
	                                               vk::Format format,
	                                               vk::Extent2D extent,
	                                               vk::ImageLayout currentLayout);

	/**
	 * @brief Converts RGBA16F half-float data to RGBA8 for PNG output.
	 *
	 * Each 4×16-bit half is converted to float.  When remapSigned is false
	 * (default), values are clamped to [0,1] then scaled to [0,255].
	 * When true, RGB channels are remapped from [-1,1] to [0,1] via
	 * (v+1)*0.5, and the alpha channel is forced to 255 (opaque).
	 * This is needed for normal-map attachments where the shader writes
	 * signed view-space normals with alpha=0.
	 *
	 * @param data        Raw half-float pixel data (8 bytes per pixel).
	 * @param width       Image width in pixels.
	 * @param height      Image height in pixels.
	 * @param remapSigned If true, remap [-1,1] → [0,1] for RGB, alpha=255.
	 * @return RGBA8 pixel data (4 bytes per pixel).
	 */
	static std::vector<uint8_t> convertHalfToU8(const void* data,
	                                             uint32_t width,
	                                             uint32_t height,
	                                             bool remapSigned = false);

	/**
	 * @brief Swizzles BGR→RGB in-place for BGRA pixel data.
	 *
	 * @param data     Pixel data to swizzle in-place.
	 * @param width    Image width in pixels.
	 * @param height   Image height in pixels.
	 * @param channels Number of channels per pixel (must be >= 3).
	 */
	static void swizzleBGRtoRGB(uint8_t* data,
	                            uint32_t width,
	                            uint32_t height,
	                            uint32_t channels);

	/**
	 * @brief Returns the byte size of a single pixel for a given format.
	 * @return Bytes per pixel, or 0 if format is unsupported.
	 */
	static uint32_t pixelByteSize(vk::Format format);

	/**
	 * @brief Returns true if the format uses a BGR channel order.
	 */
	static bool isBGRFormat(vk::Format format);

	/**
	 * @brief Returns the number of channels for a given format.
	 */
	static uint32_t channelCount(vk::Format format);
};

} // namespace neurus
