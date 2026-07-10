/**
 * @file ImageData.h
 * @brief CPU-side image data class and low‑level pixel format helpers.
 *
 * Owns raw pixel data (std::vector<uint8_t>) along with dimensions and format.
 * Provides static helpers for pixel‑byte queries, half‑float conversion,
 * BGR swizzle, and member functions for CPU‑side PNG/HDR output.
 *
 * Pure CPU data - zero Vulkan/GPU resources.  Analogous to MeshData.
 * Uses PixelFormat (not vk::Format) for format identification.
 */
#pragma once

#include "asset/PixelFormat.h"

#include <cstdint>
#include <cstring>
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
	 * HDR images (.hdr) are loaded as RGBA32F.
	 * LDR images (.png, .bmp, .jpg, .tga) are loaded as RGBA8S.
	 *
	 * @param path File path to the image.
	 */
	explicit ImageData(const std::string& path);

	/**
	 * @brief Constructs ImageData from raw pixel data, copying it into the owned vector.
	 * @param data        Non-owning pointer to raw pixel bytes.
	 * @param w           Image width in pixels.
	 * @param h           Image height in pixels.
	 * @param fmt         Pixel format.
	 * @param arrayLayers Number of array layers (default 1). Total copy size is
	 *                    w * h * PixelByteSize(fmt) * arrayLayers.
	 */
	ImageData(const void* data, uint32_t w, uint32_t h, PixelFormat fmt,
	          uint32_t arrayLayers = 1);

	// --- Validity ---

	/** @brief True if a valid image is loaded (non-zero dimensions, non-empty pixel data). */
	bool IsValid() const { return im_width > 0 && im_height > 0 && !im_pixelData.empty(); }

	// --- Getters ---

	const std::vector<uint8_t>& GetPixelData() const { return im_pixelData; }
	uint32_t GetWidth() const { return im_width; }
	uint32_t GetHeight() const { return im_height; }
	PixelFormat GetFormat() const { return im_format; }

	// -------------------------------------------------------------------
	// Format helpers (non‑static convenience wrappers)
	// -------------------------------------------------------------------

	/** @brief Bytes per pixel for the owned format. */
	uint32_t PixelByteSize() const { return neurus::PixelByteSize(im_format); }

	/** @brief Number of colour channels for the owned format. */
	uint32_t ChannelCount() const { return neurus::ChannelCount(im_format); }

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
	 * Expects RGBA32F pixel data (16 bytes per pixel).
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
	 * @brief Loads pixel data from file, filling im_pixelData, im_width, im_height, im_format.
	 * @param path File path to the image.
	 */
	void LoadFromPath(const std::string& path);

	// --- Data ---

	std::vector<uint8_t> im_pixelData;  ///< Owning pixel data (raw bytes, format-dependent byte count)
	uint32_t im_width = 0;
	uint32_t im_height = 0;
	PixelFormat im_format{};
};

} // namespace neurus
