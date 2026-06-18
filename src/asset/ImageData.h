/**
 * @file ImageData.h
 * @brief CPU-side image data class and low‑level pixel format helpers.
 *
 * Stores raw pixel data (non-owning) along with dimensions and format.
 * Provides static helpers for pixel‑byte queries, half‑float conversion,
 * BGR swizzle, and CPU‑side PNG output.
 *
 * Pure CPU data - no Vulkan/GPU resources.  Analogous to MeshData.
 */
#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace neurus {

class ImageData
{
public:
	ImageData() = default;

	/**
	 * @brief Constructs ImageData with pixel data, dimensions, and format.
	 * @param data  Non-owning pointer to raw pixel bytes.
	 * @param w     Image width in pixels.
	 * @param h     Image height in pixels.
	 * @param fmt   Vulkan pixel format.
	 */
	ImageData(const void* data, uint32_t w, uint32_t h, vk::Format fmt)
		: m_pixelData(data)
		, m_width(w)
		, m_height(h)
		, m_format(fmt)
	{
	}

	// --- Getters ---

	const void* GetPixelData() const { return m_pixelData; }
	uint32_t GetWidth() const { return m_width; }
	uint32_t GetHeight() const { return m_height; }
	vk::Format GetFormat() const { return m_format; }

	// -------------------------------------------------------------------
	// Format helpers (static)
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
	static void SwizzleBGRtoRGB(void* data, uint32_t width,
	                            uint32_t height, uint32_t channels);

	/**
	 * @brief Writes raw pixel data to a PNG file (CPU‑side only).
	 *
	 * No GPU readback - caller must provide already‑read pixel data.
	 * Used for swapchain screenshots where no Image wrapper exists.
	 */
	static bool SavePixelData(const void* rawData,
	                          vk::Format format,
	                          vk::Extent2D extent,
	                          const std::string& path,
	                          bool remapSigned = false);

private:
	static float HalfToFloat(uint16_t half);
	static void EnsureDirectory(const std::string& filePath);

	const void* m_pixelData = nullptr;  // non-owning reference
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	vk::Format m_format = vk::Format::eUndefined;
};

} // namespace neurus
