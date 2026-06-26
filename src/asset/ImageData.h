/**
 * @file ImageData.h
 * @brief CPU-side image data class and low‑level pixel format helpers.
 *
 * Owns raw pixel data (std::vector<uint8_t>) along with dimensions and format.
 * Provides static helpers for pixel‑byte queries, half‑float conversion,
 * BGR swizzle, and member functions for CPU‑side PNG/HDR output.
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
	/**
	 * @brief Default constructor. Creates an invalid/empty ImageData.
	 */
	ImageData() = default;

	/**
	 * @brief Constructs ImageData by loading an image from a file path.
	 *
	 * Auto-detects HDR vs LDR format using stb_is_hdr().
	 * HDR images (.hdr) are loaded as R32G32B32A32_SFLOAT.
	 * LDR images (.png, .bmp, .jpg, .tga) are loaded as R8G8B8A8_SRGB.
	 *
	 * @param path File path to the image.
	 */
	explicit ImageData(const std::string& path);

	/**
	 * @brief Constructs ImageData from raw pixel data, copying it into the owned vector.
	 * @param data Non-owning pointer to raw pixel bytes.
	 * @param w    Image width in pixels.
	 * @param h    Image height in pixels.
	 * @param fmt  Vulkan pixel format.
	 */
	ImageData(const void* data, uint32_t w, uint32_t h, vk::Format fmt);

	// --- Validity ---

	/** @brief True if a valid image is loaded (non-zero dimensions, non-empty pixel data). */
	bool IsValid() const { return m_width > 0 && m_height > 0 && !m_pixelData.empty(); }

	// --- Getters ---

	const std::vector<uint8_t>& GetPixelData() const { return m_pixelData; }
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

	// -------------------------------------------------------------------
	// Save (member functions — use owned pixel data)
	// -------------------------------------------------------------------

	/**
	 * @brief Writes the owned pixel data to a PNG file (CPU‑side only).
	 * @param path        Output file path.
	 * @param remapSigned If true, remap signed values for normal maps.
	 * @return true on success.
	 */
	bool SavePNG(const std::string& path, bool remapSigned = false) const;

	/**
	 * @brief Writes the owned pixel data to a Radiance .hdr file.
	 *
	 * Expects R32G32B32A32_SFLOAT pixel data (16 bytes per pixel).
	 * Output is RGBE-encoded in the Radiance HDR format.
	 *
	 * @param path Output file path (.hdr extension recommended).
	 * @return true on success.
	 */
	bool SaveHDR(const std::string& path) const;

private:
	// --- Internal helpers ---

	static float HalfToFloat(uint16_t half);
	static void EnsureDirectory(const std::string& filePath);

	/**
	 * @brief Loads pixel data from file, filling m_pixelData, m_width, m_height, m_format.
	 * @param path File path to the image.
	 */
	void LoadFromPath(const std::string& path);

	// --- Data ---

	std::vector<uint8_t> m_pixelData;  ///< Owning pixel data (format-dependent byte count)
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	vk::Format m_format = vk::Format::eUndefined;
};

} // namespace neurus
