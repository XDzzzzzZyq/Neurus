#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <string>

namespace neurus {

// --- Forward declarations ---
class Image;
class AttachmentManager;

/**
 * @brief Static utility for capturing Vulkan images to PNG files.
 *
 * Orchestrates GPU-side layout transitions and delegates GPU readback to
 * Image::ReadImageToBuffer() and CPU-side conversion / PNG writing
 * to ImageData.
 *
 * All operations are blocking (waits for queue idle).
 *
 * The constructor is deleted — this is a purely static utility class.
 */
class Screenshot
{
public:
	Screenshot() = delete;

	// Non-copyable, non-movable
	Screenshot(const Screenshot&) = delete;
	Screenshot& operator=(const Screenshot&) = delete;
	Screenshot(Screenshot&&) = delete;
	Screenshot& operator=(Screenshot&&) = delete;

	// -------------------------------------------------------------------
	// Utility
	// -------------------------------------------------------------------

	/**
	 * @brief Generates a timestamp string: "YYYYMMDD_HHMMSS".
	 * @return "{prefix}_YYYYMMDD_HHMMSS{suffix}"
	 */
	static std::string timestampedFilename(const std::string& prefix,
	                                       const std::string& suffix);

	// -------------------------------------------------------------------
	// Capture methods
	// -------------------------------------------------------------------

	/**
	 * @brief Captures a swapchain image to a PNG file.
	 *
	 * Handles layout transitions (PRESENT_SRC ↔ TRANSFER_SRC) and delegates
	 * readback to a manual staging‑buffer path.  BGR→RGB swizzle is applied
	 * automatically for BGRA swapchain formats.
	 *
	 * @return true on success.
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
	 * @brief Captures an Image attachment to a PNG file.
	 *
	 * 1. Transitions image to TRANSFER_SRC_OPTIMAL.
	 * 2. Calls vulkanImage.ReadImageToBuffer() for GPU readback.
	 * 3. Transitions back to the original layout.
	 * 4. Delegates to Texture::SaveImage() for conversion + PNG write.
	 *
	 * @param remapSigned Passed to ImageData for normal‑map [-1,1]→[0,1] remap.
	 * @return true on success.
	 */
	static bool CaptureAttachment(const vk::raii::Device& device,
	                              const vk::raii::PhysicalDevice& physicalDevice,
	                              vk::Queue queue,
	                              uint32_t queueFamilyIndex,
	                              Image& vulkanImage,
	                              const std::string& path,
	                              bool remapSigned = false);

	/**
	 * @brief Captures all G‑Buffer and post‑FX attachments to timestamped PNGs.
	 *
	 * Iterates Position, Normal, Albedo, MetallicRoughness, HDRColor,
	 * SSAO, SSR and calls CaptureAttachment() for each.
	 *
	 * @return Number of attachments captured.
	 */
	static int CaptureAllAttachments(const vk::raii::Device& device,
	                                 const vk::raii::PhysicalDevice& physicalDevice,
	                                 vk::Queue queue,
	                                 uint32_t queueFamilyIndex,
	                                 AttachmentManager& attachmentManager,
	                                 const std::string& prefix);
};

} // namespace neurus
