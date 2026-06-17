#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "TextureData.h"
#include "render/Texture.h"
#include "render/VulkanImage.h"

#include <cmath>
#include <cstring>
#include <filesystem>

namespace neurus {

// ===========================================================================
// Internal helpers
// ===========================================================================

static float halfToFloat(uint16_t half)
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

// ===========================================================================
// Format helpers
// ===========================================================================

uint32_t TextureData::PixelByteSize(vk::Format format)
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
	default:
		return 0;
	}
}

uint32_t TextureData::ChannelCount(vk::Format format)
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

bool TextureData::IsBGRFormat(vk::Format format)
{
	return format == vk::Format::eB8G8R8A8Unorm ||
	       format == vk::Format::eB8G8R8A8Srgb;
}

// ===========================================================================
// Data conversion
// ===========================================================================

std::vector<uint8_t> TextureData::ConvertHalfToU8(const void* data,
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
			float val = halfToFloat(raw);
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

void TextureData::SwizzleBGRtoRGB(uint8_t* data, uint32_t width,
                                   uint32_t height, uint32_t channels)
{
	const size_t pixelCount = static_cast<size_t>(width) * height;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		uint8_t tmp = data[i * channels + 0];
		data[i * channels + 0] = data[i * channels + 2];
		data[i * channels + 2] = tmp;
	}
}

// ===========================================================================
// Directory helper
// ===========================================================================

void TextureData::EnsureDirectory(const std::string& filePath)
{
	namespace fs = std::filesystem;
	const fs::path parent = fs::path(filePath).parent_path();
	if (!parent.empty() && !fs::exists(parent))
		fs::create_directories(parent);
}

// ===========================================================================
// SavePixelData (CPU‑side only)
// ===========================================================================

bool TextureData::SavePixelData(const void* rawData,
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
// SaveImage (GPU readback → PNG, takes VulkanImage)
// ===========================================================================

bool TextureData::SaveImage(VulkanImage& image,
                             const vk::raii::Device& device,
                             const vk::raii::PhysicalDevice& physicalDevice,
                             vk::Queue queue,
                             uint32_t queueFamilyIndex,
                             const std::string& path,
                             bool remapSigned)
{
	const vk::Image vkImage = *image.ImageHandle();
	const vk::ImageLayout prevLayout = image.CurrentLayout();

	// --- 1. Transition to TRANSFER_SRC ---
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		vk::AccessFlags srcAccess = {};
		vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		if (prevLayout != vk::ImageLayout::eUndefined)
		{
			srcAccess = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
			srcStage = vk::PipelineStageFlagBits::eAllCommands;
		}

		vk::ImageMemoryBarrier barrier(
			srcAccess, vk::AccessFlagBits::eTransferRead,
			prevLayout, vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBufs[0].pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eTransfer,
		                           {}, {}, {}, barrier);
		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();
		image.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
	}

	// --- 2. Read back ---
	std::vector<uint8_t> rawData = image.ReadImageToBuffer(
		device, physicalDevice, queue, queueFamilyIndex,
		vk::ImageLayout::eTransferSrcOptimal);

	if (rawData.empty()) return false;

	// --- 3. Transition back ---
	if (prevLayout != vk::ImageLayout::eUndefined)
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const vk::AccessFlags dstAccess =
			vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		constexpr vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eAllCommands;

		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eTransferRead, dstAccess,
			vk::ImageLayout::eTransferSrcOptimal, prevLayout,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, vkImage,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                           dstStage, {}, {}, {}, barrier);
		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();
		image.SetCurrentLayout(prevLayout);
	}

	// --- 4. Save pixel data ---
	return SavePixelData(rawData.data(), image.Format(), image.Extent(), path, remapSigned);
}

// ===========================================================================
// SaveTexture (convenience — delegates to SaveImage)
// ===========================================================================

bool TextureData::SaveTexture(const vk::raii::Device& device,
                               const vk::raii::PhysicalDevice& physicalDevice,
                               vk::Queue queue,
                               uint32_t queueFamilyIndex,
                               Texture& texture,
                               const std::string& path,
                               bool remapSigned)
{
	VulkanImage* img = texture.GetImage();
	if (!img) return false;
	return SaveImage(*img, device, physicalDevice, queue, queueFamilyIndex, path, remapSigned);
}

// ===========================================================================
// Texture cache (from former TextureLib)
// ===========================================================================

std::unordered_map<std::string, TextureData::TextureRes> TextureData::s_cache;

TextureData::TextureRes TextureData::LoadTexture(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue queue,
	uint32_t queueFamilyIndex,
	const char* path,
	vk::Format format,
	const SamplerConfig& config)
{
	std::string key(path);
	auto it = s_cache.find(key);
	if (it != s_cache.end())
		return it->second;

	auto tex = std::make_shared<Texture>(
		Texture::FromFile(device, physicalDevice, queue, queueFamilyIndex, path, format, config));
	s_cache[key] = tex;
	return tex;
}

void TextureData::UnloadTexture(const std::string& path)
{
	s_cache.erase(path);
}

void TextureData::ClearCache()
{
	s_cache.clear();
}

size_t TextureData::CacheSize()
{
	return s_cache.size();
}

} // namespace neurus
