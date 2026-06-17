/**
 * @file TextureData.h
 * @brief Texture loading, saving, and low‑level image data conversion.
 *
 * Combines the former ImageData and TextureLib into a single utility class:
 *   - LoadTexture()   — load an image file to GPU (cached)
 *   - SaveTexture()   — read back a Texture from GPU and write PNG
 *   - SaveImage()     — convert raw pixel data and write PNG (no GPU ops)
 *   - Format helpers  — pixel‑byte queries, half‑float conversion, BGR swizzle
 *
 * All methods are static.  Non‑copyable, non‑movable.
 */
#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/Texture.h"

namespace neurus {

// --- Forward declarations ---
class VulkanImage;

class TextureData
{
public:
	TextureData() = delete;
	TextureData(const TextureData&) = delete;
	TextureData& operator=(const TextureData&) = delete;
	TextureData(TextureData&&) = delete;
	TextureData& operator=(TextureData&&) = delete;

	// -------------------------------------------------------------------
	// Texture loading (cached)
	// -------------------------------------------------------------------

	/** @brief Shared pointer to a Texture. */
	using TextureRes = std::shared_ptr<Texture>;

	/**
	 * @brief Loads (or retrieves from cache) a texture from a file path.
	 * @return Shared pointer; may be invalid if loading fails.
	 */
	static TextureRes LoadTexture(const vk::raii::Device& device,
	                              const vk::raii::PhysicalDevice& physicalDevice,
	                              vk::Queue queue,
	                              uint32_t queueFamilyIndex,
	                              const char* path,
	                              vk::Format format,
	                              const SamplerConfig& config = {});

	/** @brief Removes a texture from the cache by path. */
	static void UnloadTexture(const std::string& path);

	/** @brief Clears all cached textures. */
	static void ClearCache();

	/** @brief Returns the number of cached textures. */
	static size_t CacheSize();

	// -------------------------------------------------------------------
	// Texture saving (GPU readback → PNG)
	// -------------------------------------------------------------------

	/**
	 * @brief Reads back a Texture from the GPU and writes it as PNG.
	 *
	 * Convenience wrapper that delegates to SaveImage() via texture.GetImage().
	 * @return true on success.
	 */
	static bool SaveTexture(const vk::raii::Device& device,
	                        const vk::raii::PhysicalDevice& physicalDevice,
	                        vk::Queue queue,
	                        uint32_t queueFamilyIndex,
	                        Texture& texture,
	                        const std::string& path,
	                        bool remapSigned = false);

	// -------------------------------------------------------------------
	// Image saving (GPU readback → PNG)
	// -------------------------------------------------------------------

	/**
	 * @brief Reads back a VulkanImage from the GPU and writes it as PNG.
	 *
	 * Handles layout transitions, GPU readback, format conversion,
	 * and PNG output.  The image is left in the layout it was found in.
	 *
	 * @param image       VulkanImage to capture (layout is temporarily transitioned).
	 * @param remapSigned Passed to ConvertHalfToU8 for normal‑map data.
	 * @return true on success.
	 */
	static bool SaveImage(VulkanImage& image,
	                      const vk::raii::Device& device,
	                      const vk::raii::PhysicalDevice& physicalDevice,
	                      vk::Queue queue,
	                      uint32_t queueFamilyIndex,
	                      const std::string& path,
	                      bool remapSigned = false);

	/**
	 * @brief Writes raw pixel data to a PNG file (CPU‑side only).
	 *
	 * No GPU readback — caller must provide already‑read pixel data.
	 * Used for swapchain screenshots where no VulkanImage wrapper exists.
	 */
	static bool SavePixelData(const void* rawData,
	                          vk::Format format,
	                          vk::Extent2D extent,
	                          const std::string& path,
	                          bool remapSigned = false);

	// -------------------------------------------------------------------
	// Format helpers
	// -------------------------------------------------------------------

	/** @brief Bytes per pixel, or 0 if unsupported. */
	static uint32_t PixelByteSize(vk::Format format);

	/** @brief Number of colour channels. */
	static uint32_t ChannelCount(vk::Format format);

	/** @brief True for BGRA formats. */
	static bool IsBGRFormat(vk::Format format);

	/**
	 * @brief Converts RGBA16F half‑float data to RGBA8.
	 * @param remapSigned If true, remap [-1,1]→[0,1] for RGB, handle
	 *                    background transparency, force alpha to 255
	 *                    on geometry pixels.
	 */
	static std::vector<uint8_t> ConvertHalfToU8(const void* data,
	                                             uint32_t width,
	                                             uint32_t height,
	                                             bool remapSigned = false);

	/** @brief Swaps R↔B channel in place for BGRA data. */
	static void SwizzleBGRtoRGB(uint8_t* data, uint32_t width,
	                            uint32_t height, uint32_t channels);

private:
	static float HalfToFloat(uint16_t half);
	static void EnsureDirectory(const std::string& filePath);

	// --- Texture cache ---
	static std::unordered_map<std::string, TextureRes> s_cache;
};

} // namespace neurus
