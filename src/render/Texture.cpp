#include "Texture.h"
#include "VulkanBuffer.h"

#include "Log.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace neurus {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * @brief Returns the byte size of a single pixel for a given format.
 */
static vk::DeviceSize pixelByteSize(vk::Format format)
{
	switch (format)
	{
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eR8G8B8A8Unorm:
		return 4;
	case vk::Format::eR32G32B32A32Sfloat:
		return 16;
	case vk::Format::eR32G32B32Sfloat:
		return 12;
	case vk::Format::eR32G32Sfloat:
		return 8;
	case vk::Format::eR32Sfloat:
		return 4;
	default:
		// Conservative fallback - treat as RGBA8
		return 4;
	}
}

/**
 * @brief Creates a vk::raii::Sampler from the given config and mip level count.
 */
static vk::raii::Sampler createSampler(const vk::raii::Device& device,
                                       const SamplerConfig& config,
                                       uint32_t mipLevels)
{
	if (mipLevels == 0)
	{
		mipLevels = 1;
	}

	const vk::SamplerCreateInfo samplerCI(
		{},                                          // flags
		config.magFilter,
		config.minFilter,
		config.mipmapMode,
		config.addressModeU,
		config.addressModeV,
		config.addressModeW,
		0.0f,                                        // mipLodBias
		VK_FALSE,                                    // anisotropyEnable
		0.0f,                                        // maxAnisotropy
		VK_FALSE,                                    // compareEnable
		vk::CompareOp::eNever,
		0.0f,                                        // minLod
		static_cast<float>(mipLevels),               // maxLod
		vk::BorderColor::eFloatOpaqueBlack,
		VK_FALSE);                                   // unnormalizedCoordinates

	return vk::raii::Sampler(device, samplerCI);
}

// ---------------------------------------------------------------------------
// Factory: FromFile
// ---------------------------------------------------------------------------

Texture Texture::FromFile(const vk::raii::Device& device,
                          const vk::raii::PhysicalDevice& physicalDevice,
                          vk::Queue queue,
                          const uint32_t queueFamilyIndex,
                          const char* path,
                          const vk::Format format,
                          const SamplerConfig& config)
{
	int width = 0, height = 0, channels = 0;
	stbi_uc* data = nullptr;
	float* dataf = nullptr;
	const void* pixelData = nullptr;
	vk::DeviceSize dataSize = 0;

	// Determine whether to load as HDR (float) or LDR (byte)
	const bool isHdr = (format == vk::Format::eR32G32B32A32Sfloat);

	if (isHdr)
	{
		dataf = stbi_loadf(path, &width, &height, &channels, 4); // force RGBA
		pixelData = dataf;
	}
	else
	{
		data = stbi_load(path, &width, &height, &channels, 4); // force RGBA
		pixelData = data;
	}

	if (!pixelData || width <= 0 || height <= 0)
	{
		// Free if partial allocation occurred
		if (isHdr) { stbi_image_free(dataf); }
		else       { stbi_image_free(data); }
		return Texture{}; // invalid
	}

	dataSize = static_cast<vk::DeviceSize>(width) * static_cast<vk::DeviceSize>(height) * pixelByteSize(format);

	Texture tex = createFromPixelData(device, physicalDevice, queue, queueFamilyIndex,
	                                  static_cast<uint32_t>(width),
	                                  static_cast<uint32_t>(height),
	                                  pixelData, dataSize, format,
	                                  /*generateMipmaps=*/true, config);

	// Free STB data
	if (isHdr) { stbi_image_free(dataf); }
	else       { stbi_image_free(data); }

	return tex;
}

// ---------------------------------------------------------------------------
// Factory: FromData
// ---------------------------------------------------------------------------

Texture Texture::FromData(const vk::raii::Device& device,
                          const vk::raii::PhysicalDevice& physicalDevice,
                          vk::Queue queue,
                          const uint32_t queueFamilyIndex,
                          const uint32_t width,
                          const uint32_t height,
                          const void* pixelData,
                          const vk::Format format,
                          const SamplerConfig& config)
{
	if (!pixelData || width == 0 || height == 0)
	{
		return Texture{}; // invalid
	}

	const vk::DeviceSize dataSize = static_cast<vk::DeviceSize>(width)
	                                * static_cast<vk::DeviceSize>(height)
	                                * pixelByteSize(format);

	return createFromPixelData(device, physicalDevice, queue, queueFamilyIndex,
	                           width, height, pixelData, dataSize, format,
	                           /*generateMipmaps=*/true, config);
}

// ---------------------------------------------------------------------------
// Factory: ForAttachment
// ---------------------------------------------------------------------------

Texture Texture::ForAttachment(const vk::raii::Device& device,
                               const vk::raii::PhysicalDevice& physicalDevice,
                               const vk::Extent2D extent,
                               const vk::Format format,
                               const vk::ImageUsageFlags usage,
                               const SamplerConfig& config)
{
	if (extent.width == 0 || extent.height == 0)
	{
		return Texture{};
	}

	Texture tex;

	try
	{
		tex.m_image = std::make_unique<VulkanImage>(
			device, physicalDevice,
			extent, format, usage,
			/*mipLevels=*/1,
			VulkanImage::ImageType::e2D);

		// Create sampler only if the image will be sampled
		if (usage & vk::ImageUsageFlagBits::eSampled)
		{
			tex.m_sampler = createSampler(device, config, 1);
		}
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Texture::ForAttachment failed: " << e.what());
		return Texture{};
	}

	return tex;
}

// ---------------------------------------------------------------------------
// Internal: createFromPixelData
// ---------------------------------------------------------------------------

Texture Texture::createFromPixelData(const vk::raii::Device& device,
                                     const vk::raii::PhysicalDevice& physicalDevice,
                                     const vk::Queue queue,
                                     const uint32_t queueFamilyIndex,
                                     const uint32_t width,
                                     const uint32_t height,
                                     const void* pixelData,
                                     const vk::DeviceSize dataSize,
                                     const vk::Format format,
                                     const bool generateMipmaps,
                                     const SamplerConfig& config)
{
	Texture tex;

	const uint32_t mipLevels = generateMipmaps
		? computeMipLevels(width, height)
		: 1u;

	try
	{
		// --- 1. Create VulkanImage ---
		const vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst
		                                  | vk::ImageUsageFlagBits::eTransferSrc
		                                  | vk::ImageUsageFlagBits::eSampled;

		tex.m_image = std::make_unique<VulkanImage>(
			device, physicalDevice,
			vk::Extent2D{width, height}, format, usage,
			mipLevels,
			VulkanImage::ImageType::e2D);

		// --- 2. Create staging buffer (host-visible) ---
		const vk::BufferUsageFlags stagingUsage = vk::BufferUsageFlagBits::eTransferSrc;
		const vk::MemoryPropertyFlags stagingProps = vk::MemoryPropertyFlagBits::eHostVisible
		                                             | vk::MemoryPropertyFlagBits::eHostCoherent;

		VulkanBuffer stagingBuffer(device, physicalDevice, queue, queueFamilyIndex,
		                           dataSize, stagingUsage, stagingProps);

		// Copy pixel data into staging buffer
		void* mapped = stagingBuffer.Map();
		std::memcpy(mapped, pixelData, static_cast<size_t>(dataSize));
		stagingBuffer.Unmap();

		// --- 3. Create transient command pool and one-shot command buffer ---
		const vk::CommandPoolCreateInfo poolCI(
			vk::CommandPoolCreateFlagBits::eTransient, queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);

		const vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
		cmdBufs[0].begin(beginInfo);

		// --- 4. Transition all levels: UNDEFINED → TRANSFER_DST ---
		tex.m_image->TransitionLayout(cmdBufs[0],
		                              vk::ImageLayout::eUndefined,
		                              vk::ImageLayout::eTransferDstOptimal,
		                              0, mipLevels);

		// --- 5. Copy staging buffer → image (level 0 only) ---
		const vk::BufferImageCopy copyRegion(
			0,  // bufferOffset
			0,  // bufferRowLength (tightly packed)
			0,  // bufferImageHeight (tightly packed)
			vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
			vk::Offset3D{0, 0, 0},
			vk::Extent3D{width, height, 1});

		cmdBufs[0].copyBufferToImage(
			stagingBuffer.buffer(),
			tex.m_image->ImageHandle(),
			vk::ImageLayout::eTransferDstOptimal,
			{ copyRegion });

		// --- 6. Generate mipmaps (if requested) ---
		// GenerateMipmaps expects all levels in TRANSFER_DST already,
		// which they are after step 4.
		if (generateMipmaps && mipLevels > 1)
		{
			tex.m_image->GenerateMipmaps(cmdBufs[0]);
		}
		else
		{
			// Single mip level: transition directly to SHADER_READ_ONLY
			tex.m_image->TransitionLayout(cmdBufs[0],
			                              vk::ImageLayout::eTransferDstOptimal,
			                              vk::ImageLayout::eShaderReadOnlyOptimal,
			                              0, 1);
		}

		cmdBufs[0].end();

		// --- 7. Submit and wait ---
		const vk::SubmitInfo submitInfo({}, {}, *cmdBufs[0]);
		queue.submit(submitInfo);
		queue.waitIdle();

		// --- 8. Create sampler ---
		tex.m_sampler = createSampler(device, config, mipLevels);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Texture::createFromPixelData failed: " << e.what());
		return Texture{};
	}

	return tex;
}

// ---------------------------------------------------------------------------
// computeMipLevels
// ---------------------------------------------------------------------------

uint32_t Texture::computeMipLevels(const uint32_t width, const uint32_t height)
{
	const uint32_t maxDim = std::max(width, height);
	return static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDim)))) + 1u;
}

} // namespace neurus
