#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "ImageData.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace neurus {

// ===========================================================================
// Internal helpers
// ===========================================================================

float ImageData::HalfToFloat(uint16_t half)
{
	const uint32_t sign     = (half & 0x8000u) >> 15;
	const uint32_t exponent = (half & 0x7C00u) >> 10;
	const uint32_t mantissa =  half & 0x03FFu;

	uint32_t f32;
	if (exponent == 0)
	{
		if (mantissa == 0) { f32 = sign << 31; }
		else
		{
			uint32_t m = mantissa;
			int e = -14;
			while ((m & 0x0400u) == 0) { m <<= 1; --e; }
			m &= 0x03FFu;
			f32 = (sign << 31) | ((uint32_t)(e + 127) << 23) | (m << 13);
		}
	}
	else if (exponent == 0x1F)
	{
		f32 = (sign << 31) | 0x7F800000u | (mantissa << 13);
	}
	else
	{
		f32 = (sign << 31) | ((uint32_t)(exponent - 15 + 127) << 23) | (mantissa << 13);
	}

	float result;
	std::memcpy(&result, &f32, sizeof(float));
	return result;
}

void ImageData::EnsureDirectory(const std::string& filePath)
{
	namespace fs = std::filesystem;
	const fs::path parent = fs::path(filePath).parent_path();
	if (!parent.empty() && !fs::exists(parent))
		fs::create_directories(parent);
}

// ===========================================================================
// Format helpers
// ===========================================================================

uint32_t ImageData::PixelByteSize(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8A8Unorm:
	case vk::Format::eB8G8R8A8Srgb:
		return 4;
	case vk::Format::eR16G16B16A16Sfloat:
	case vk::Format::eR16G16B16A16Unorm:
	case vk::Format::eR16G16B16A16Snorm:
		return 8;
	case vk::Format::eR8Unorm:
	case vk::Format::eR8Srgb:
		return 1;
	case vk::Format::eD32Sfloat:
		return 4;
	default:
		return 0;
	}
}

uint32_t ImageData::ChannelCount(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eR8Unorm:
	case vk::Format::eR8Srgb:
		return 1;
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8A8Unorm:
	case vk::Format::eB8G8R8A8Srgb:
	case vk::Format::eR16G16B16A16Sfloat:
	case vk::Format::eR16G16B16A16Unorm:
	case vk::Format::eR16G16B16A16Snorm:
	default:
		return 4;
	}
}

bool ImageData::IsBGRFormat(vk::Format format)
{
	return format == vk::Format::eB8G8R8A8Unorm ||
	       format == vk::Format::eB8G8R8A8Srgb;
}

// ===========================================================================
// Data conversion
// ===========================================================================

std::vector<uint8_t> ImageData::ConvertHalfToU8(const void* data,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 bool remapSigned)
{
	const auto* src = static_cast<const uint16_t*>(data);
	const size_t pixelCount = static_cast<size_t>(width) * height;
	std::vector<uint8_t> result(pixelCount * 4);

	for (size_t i = 0; i < pixelCount; ++i)
	{
		const bool isBackground = remapSigned
			&& (src[i * 4 + 0] == 0)
			&& (src[i * 4 + 1] == 0)
			&& (src[i * 4 + 2] == 0);

		for (int c = 0; c < 4; ++c)
		{
			uint16_t raw = src[i * 4 + c];
			float val = HalfToFloat(raw);
			if (remapSigned && c < 3 && !isBackground)
				val = (val + 1.0f) * 0.5f;
			val = (val < 0.0f) ? 0.0f : ((val > 1.0f) ? 1.0f : val);
			result[i * 4 + c] = static_cast<uint8_t>(val * 255.0f + 0.5f);
		}
		if (remapSigned && !isBackground)
			result[i * 4 + 3] = 255;
	}
	return result;
}

void ImageData::SwizzleBGRtoRGB(void* data, uint32_t width,
                                 uint32_t height, uint32_t channels)
{
	auto* bytes = static_cast<uint8_t*>(data);
	const size_t pixelCount = static_cast<size_t>(width) * height;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		uint8_t tmp = bytes[i * channels + 0];
		bytes[i * channels + 0] = bytes[i * channels + 2];
		bytes[i * channels + 2] = tmp;
	}
}

// ===========================================================================
// SavePixelData (CPU‑side only)
// ===========================================================================

bool ImageData::SavePixelData(const void* rawData,
                               vk::Format format,
                               vk::Extent2D extent,
                               const std::string& path,
                               bool remapSigned)
{
	const uint32_t channels = ChannelCount(format);
	std::vector<uint8_t> pngData;

	if (format == vk::Format::eR16G16B16A16Sfloat ||
	    format == vk::Format::eR16G16B16A16Unorm ||
	    format == vk::Format::eR16G16B16A16Snorm)
	{
		pngData = ConvertHalfToU8(rawData, extent.width, extent.height, remapSigned);
	}
	else
	{
		const size_t byteCount = static_cast<size_t>(extent.width) * extent.height *
		                         PixelByteSize(format);
		pngData.assign(static_cast<const uint8_t*>(rawData),
		               static_cast<const uint8_t*>(rawData) + byteCount);
		if (IsBGRFormat(format) && channels >= 3)
			SwizzleBGRtoRGB(pngData.data(), extent.width, extent.height, channels);
	}

	EnsureDirectory(path);
	const int stride = static_cast<int>(extent.width) * static_cast<int>(channels);
	return stbi_write_png(path.c_str(),
	                      static_cast<int>(extent.width),
	                      static_cast<int>(extent.height),
	                      static_cast<int>(channels),
	                      pngData.data(), stride) != 0;
}

// ===========================================================================
// SavePixelDataHDR (Radiance .hdr format)
// ===========================================================================

bool ImageData::SavePixelDataHDR(const void* pixelData,
                                  uint32_t width,
                                  uint32_t height,
                                  const std::string& path)
{
	const auto* src = static_cast<const float*>(pixelData);

	std::string out;
	out.reserve(256 + static_cast<size_t>(width) * height * 4);

	// --- Radiance header ---
	out += "#?RADIANCE\n";
	out += "FORMAT=32-bit_rle_rgbe\n";
	out += "\n";
	out += "-Y " + std::to_string(height) + " +X " + std::to_string(width) + "\n";

	// --- RGBE pixel data (flat, non-RLE) ---
	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
			const float r = src[idx + 0];
			const float g = src[idx + 1];
			const float b = src[idx + 2];

			// Find maximum channel and encode as RGBE
			const float maxVal = std::max({r, g, b});

			if (maxVal < 1e-32f)
			{
				out.push_back(static_cast<char>(0));
				out.push_back(static_cast<char>(0));
				out.push_back(static_cast<char>(0));
				out.push_back(static_cast<char>(0));
			}
			else
			{
				int e = 0;
				float m = std::frexp(maxVal, &e);
				const float scale = (m * 255.9999f) / maxVal;

				const uint8_t re = static_cast<uint8_t>(r * scale);
				const uint8_t ge = static_cast<uint8_t>(g * scale);
				const uint8_t be = static_cast<uint8_t>(b * scale);
				const uint8_t ee = static_cast<uint8_t>(e + 128);

				out.push_back(static_cast<char>(re));
				out.push_back(static_cast<char>(ge));
				out.push_back(static_cast<char>(be));
				out.push_back(static_cast<char>(ee));
			}
		}
	}

	// --- Write file ---
	EnsureDirectory(path);

	std::ofstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		return false;
	}

	file.write(out.data(), out.size());
	return file.good();
}

// ===========================================================================
// LoadFromPath (generic image loader: auto-detects HDR vs LDR)
// ===========================================================================

ImageLoadResult ImageData::LoadFromPath(const std::string& path)
{
	ImageLoadResult result;

	if (stbi_is_hdr(path.c_str()))
	{
		int w = 0, h = 0, c = 0;
		float* data = stbi_loadf(path.c_str(), &w, &h, &c, 4);
		if (!data || w <= 0 || h <= 0)
		{
			NEURUS_ERR("[ImageData] Failed to load HDR image from path: " << path);
			return result;
		}

		NEURUS_LOG("[ImageData] Loaded HDR: " << path << " (" << w << "x" << h << ", " << c << " channels)");

		result.width = static_cast<uint32_t>(w);
		result.height = static_cast<uint32_t>(h);
		result.format = vk::Format::eR32G32B32A32Sfloat;

		const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4 * sizeof(float);
		result.pixelData.resize(byteCount);
		std::memcpy(result.pixelData.data(), data, byteCount);
		stbi_image_free(data);
	}
	else
	{
		int w = 0, h = 0, c = 0;
		stbi_uc* data = stbi_load(path.c_str(), &w, &h, &c, 4);
		if (!data || w <= 0 || h <= 0)
		{
			NEURUS_ERR("[ImageData] Failed to load LDR image from path: " << path);
			return result;
		}

		NEURUS_LOG("[ImageData] Loaded LDR: " << path << " (" << w << "x" << h << ", " << c << " channels)");

		result.width = static_cast<uint32_t>(w);
		result.height = static_cast<uint32_t>(h);
		result.format = vk::Format::eR8G8B8A8Srgb;

		const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
		result.pixelData.resize(byteCount);
		std::memcpy(result.pixelData.data(), data, byteCount);
		stbi_image_free(data);
	}

	return result;
}

} // namespace neurus
