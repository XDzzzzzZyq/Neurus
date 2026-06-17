#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "Screenshot.h"
#include "VulkanImage.h"
#include "AttachmentManager.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace neurus {

// ===========================================================================
// Internal helpers
// ===========================================================================

/**
 * @brief Finds the first memory type index matching the given requirements.
 */
static uint32_t findMemoryType(const vk::raii::PhysicalDevice& pd,
                               uint32_t typeBits,
                               vk::MemoryPropertyFlags required)
{
	auto memProps = pd.getMemoryProperties();
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		if ((typeBits & (1u << i)) &&
		    (memProps.memoryTypes[i].propertyFlags & required) == required)
		{
			return i;
		}
	}
	throw std::runtime_error("Screenshot: No suitable memory type found.");
}

// ---------------------------------------------------------------------------
// Format helpers
// ---------------------------------------------------------------------------

uint32_t Screenshot::pixelByteSize(vk::Format format)
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

uint32_t Screenshot::channelCount(vk::Format format)
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

bool Screenshot::isBGRFormat(vk::Format format)
{
	return format == vk::Format::eB8G8R8A8Unorm ||
	       format == vk::Format::eB8G8R8A8Srgb;
}

// ---------------------------------------------------------------------------
// Directory / filename helpers
// ---------------------------------------------------------------------------

void Screenshot::ensureDirectory(const std::string& filePath)
{
	namespace fs = std::filesystem;
	const fs::path parent = fs::path(filePath).parent_path();
	if (!parent.empty() && !fs::exists(parent))
	{
		fs::create_directories(parent);
	}
}

std::string Screenshot::timestampedFilename(const std::string& prefix,
                                            const std::string& suffix)
{
	const auto now = std::chrono::system_clock::now();
	const auto timeT = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
	localtime_s(&tm, &timeT);

	std::ostringstream oss;
	oss << prefix << "_"
	    << std::put_time(&tm, "%Y%m%d_%H%M%S")
	    << suffix;
	return oss.str();
}

// ---------------------------------------------------------------------------
// Half-float conversion
// ---------------------------------------------------------------------------

/**
 * @brief Converts a 16-bit IEEE 754 half-float to 32-bit float.
 */
static float halfToFloat(uint16_t half)
{
	// IEEE 754 half: 1 sign, 5 exponent, 10 mantissa
	const uint32_t sign     = (half & 0x8000u) << 16;
	const uint32_t exponent = (half & 0x7C00u) >> 10;
	const uint32_t mantissa = (half & 0x03FFu);

	uint32_t f32;
	if (exponent == 0)
	{
		// Zero or denormalized
		if (mantissa == 0)
		{
			f32 = sign;  // zero
		}
		else
		{
			// Denormalized: convert to normalized float
			// exp = 2^(-14), mantissa leading 0
			uint32_t m = mantissa;
			int e = -14;
			while ((m & 0x0400u) == 0)
			{
				m <<= 1;
				--e;
			}
			m &= 0x03FFu;  // Remove leading 1
			f32 = sign | ((uint32_t)(e + 127) << 23) | (m << 13);
		}
	}
	else if (exponent == 31)
	{
		// Inf or NaN
		f32 = sign | 0x7F800000u | (mantissa << 13);
	}
	else
	{
		// Normalized: exponent bias 15 → 127
		f32 = sign | ((uint32_t)(exponent - 15 + 127) << 23) | (mantissa << 13);
	}

	float result;
	std::memcpy(&result, &f32, sizeof(float));
	return result;
}

std::vector<uint8_t> Screenshot::convertHalfToU8(const void* data,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  bool remapSigned)
{
	const auto* src = static_cast<const uint16_t*>(data);
	const size_t pixelCount = static_cast<size_t>(width) * height;
	std::vector<uint8_t> result(pixelCount * 4);

	for (size_t i = 0; i < pixelCount; ++i)
	{
		// Detect background pixels: the geometry pass clear colour is (0,0,0,0).
		// Pixels that are exactly zero in all three RGB channels are cleared
		// background — leave them transparent black.
		const bool isBackground = remapSigned
			&& (src[i * 4 + 0] == 0)
			&& (src[i * 4 + 1] == 0)
			&& (src[i * 4 + 2] == 0);

		for (int c = 0; c < 4; ++c)
		{
			// Read as raw uint16_t (little-endian ordering - GPU uses native endian)
			// Vulkan spec: data in buffer matches host endian
			uint16_t raw = src[i * 4 + c];

			// Convert to float
			float val = halfToFloat(raw);

			if (remapSigned && c < 3 && !isBackground)
			{
				// Remap [-1,1] → [0,1] for normal-map RGB channels
				val = (val + 1.0f) * 0.5f;
			}

			// Clamp to [0, 1] and scale to [0, 255] for PNG
			// For HDR values > 1, they clamp to white; negative values clamp to black.
			val = (val < 0.0f) ? 0.0f : ((val > 1.0f) ? 1.0f : val);
			result[i * 4 + c] = static_cast<uint8_t>(val * 255.0f + 0.5f);
		}

		// Force alpha to opaque for geometry pixels in signed-remap mode.
		// Background stays transparent (alpha=0).
		if (remapSigned && !isBackground)
		{
			result[i * 4 + 3] = 255;
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
// BGR → RGB swizzle
// ---------------------------------------------------------------------------

void Screenshot::swizzleBGRtoRGB(uint8_t* data,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t channels)
{
	const size_t pixelCount = static_cast<size_t>(width) * height;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		// Swap R (index 0) and B (index 2)
		uint8_t tmp = data[i * channels + 0];
		data[i * channels + 0] = data[i * channels + 2];
		data[i * channels + 2] = tmp;
	}
}

// ---------------------------------------------------------------------------
// GPU readback
// ---------------------------------------------------------------------------

std::vector<uint8_t> Screenshot::readImageToBuffer(const vk::raii::Device& device,
                                                    const vk::raii::PhysicalDevice& physicalDevice,
                                                    vk::Queue queue,
                                                    uint32_t queueFamilyIndex,
                                                    vk::Image image,
                                                    vk::Format format,
                                                    vk::Extent2D extent,
                                                    vk::ImageLayout currentLayout)
{
	const uint32_t bytesPerPixel = pixelByteSize(format);
	if (bytesPerPixel == 0)
	{
		return {};
	}

	const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(extent.width) *
	                                 extent.height * bytesPerPixel;

	// --- 1. Create staging buffer (host-visible, transfer dst) ---
	vk::BufferCreateInfo stagingCI({}, imageSize,
		vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuffer(device, stagingCI);

	auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
	uint32_t stagingMemType = findMemoryType(physicalDevice,
		stagingMemReqs.memoryTypeBits,
		vk::MemoryPropertyFlagBits::eHostVisible |
		vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::MemoryAllocateInfo stagingAlloc(stagingMemReqs.size, stagingMemType);
	vk::raii::DeviceMemory stagingMemory(device, stagingAlloc);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	// --- 2. Create transient command pool + one-shot command buffer ---
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
	                                 queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, poolCI);

	vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocInfo);

	// --- 3. Record vkCmdCopyImageToBuffer ---
	cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	// Determine buffer image copy region
	vk::BufferImageCopy copyRegion;
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;  // tightly packed
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource = vk::ImageSubresourceLayers(
		vk::ImageAspectFlagBits::eColor, 0, 0, 1);
	copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
	copyRegion.imageExtent = vk::Extent3D(extent.width, extent.height, 1);

	cmdBufs[0].copyImageToBuffer(image, currentLayout, *stagingBuffer, copyRegion);

	// --- 4. Insert barrier to make the copy visible to host reads ---
	vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite,
	                          vk::AccessFlagBits::eHostRead);
	cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                           vk::PipelineStageFlagBits::eHost,
	                           {}, {barrier}, {}, {});

	cmdBufs[0].end();

	// --- 5. Submit and wait ---
	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
	queue.submit(submitInfo);
	queue.waitIdle();

	// --- 6. Map and copy pixel data ---
	std::vector<uint8_t> pixelData(static_cast<size_t>(imageSize));
	void* mapped = stagingMemory.mapMemory(0, imageSize);
	std::memcpy(pixelData.data(), mapped, static_cast<size_t>(imageSize));
	stagingMemory.unmapMemory();

	return pixelData;
}

// ===========================================================================
// CaptureSwapchain
// ===========================================================================

bool Screenshot::CaptureSwapchain(const vk::raii::Device& device,
                                   const vk::raii::PhysicalDevice& physicalDevice,
                                   vk::Queue queue,
                                   uint32_t queueFamilyIndex,
                                   vk::Image image,
                                   vk::Format format,
                                   vk::Extent2D extent,
                                   const std::string& path)
{
	const uint32_t bytesPerPixel = pixelByteSize(format);
	if (bytesPerPixel == 0)
	{
		return false;
	}

	// --- 1. Create transient command pool ---
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
	                                 queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, poolCI);

	vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocInfo);

	// --- 2. Transition PRESENT_SRC → TRANSFER_SRC ---
	cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	{
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eMemoryRead,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageLayout::ePresentSrcKHR,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			image,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe,
		                           vk::PipelineStageFlagBits::eTransfer,
		                           {}, {}, {}, barrier);
	}

	// --- 3. Copy image → staging buffer ---
	const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(extent.width) *
	                                 extent.height * bytesPerPixel;

	vk::BufferCreateInfo stagingCI({}, imageSize, vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuffer(device, stagingCI);

	auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
	uint32_t stagingMemType = findMemoryType(physicalDevice,
		stagingMemReqs.memoryTypeBits,
		vk::MemoryPropertyFlagBits::eHostVisible |
		vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::MemoryAllocateInfo stagingAlloc(stagingMemReqs.size, stagingMemType);
	vk::raii::DeviceMemory stagingMemory(device, stagingAlloc);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	{
		vk::BufferImageCopy copyRegion;
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource = vk::ImageSubresourceLayers(
			vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
		copyRegion.imageExtent = vk::Extent3D(extent.width, extent.height, 1);

		cmdBufs[0].copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal,
		                             *stagingBuffer, copyRegion);
	}

	// --- 4. Host read barrier ---
	{
		vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite,
		                          vk::AccessFlagBits::eHostRead);
		cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                           vk::PipelineStageFlagBits::eHost,
		                           {}, {barrier}, {}, {});
	}

	// --- 5. Transition back TRANSFER_SRC → PRESENT_SRC ---
	{
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eTransferRead,
			vk::AccessFlagBits::eMemoryRead,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			image,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                           vk::PipelineStageFlagBits::eBottomOfPipe,
		                           {}, {}, {}, barrier);
	}

	cmdBufs[0].end();

	// --- 6. Submit and wait ---
	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
	queue.submit(submitInfo);
	queue.waitIdle();

	// --- 7. Map and read pixel data ---
	std::vector<uint8_t> pixelData(static_cast<size_t>(imageSize));
	void* mapped = stagingMemory.mapMemory(0, imageSize);
	std::memcpy(pixelData.data(), mapped, static_cast<size_t>(imageSize));
	stagingMemory.unmapMemory();

	// --- 8. Swizzle BGR → RGB if needed ---
	if (isBGRFormat(format))
	{
		swizzleBGRtoRGB(pixelData.data(), extent.width, extent.height, 4);
	}

	// --- 9. Write PNG ---
	ensureDirectory(path);

	const int stride = static_cast<int>(extent.width) * 4;
	const int writeResult = stbi_write_png(path.c_str(),
	                                       static_cast<int>(extent.width),
	                                       static_cast<int>(extent.height),
	                                       4,  // RGBA
	                                       pixelData.data(),
	                                       stride);
	return writeResult != 0;
}

// ===========================================================================
// CaptureAttachment
// ===========================================================================

bool Screenshot::CaptureAttachment(const vk::raii::Device& device,
                                    const vk::raii::PhysicalDevice& physicalDevice,
                                    vk::Queue queue,
                                    uint32_t queueFamilyIndex,
                                    VulkanImage& vulkanImage,
                                    const std::string& path,
                                    bool remapSigned)
{
	const vk::Image image = *vulkanImage.ImageHandle();
	const vk::Format format = vulkanImage.Format();
	const vk::Extent2D extent = vulkanImage.Extent();

	const uint32_t bytesPerPixel = pixelByteSize(format);
	if (bytesPerPixel == 0)
	{
		return false;
	}

	// --- 1. Query current layout and transition to TRANSFER_SRC_OPTIMAL ---
	const vk::ImageLayout prevLayout = vulkanImage.CurrentLayout();

	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		// Use the tracked layout as oldLayout so the barrier is always correct,
		// regardless of whether the image is in SHADER_READ_ONLY, TRANSFER_SRC,
		// UNDEFINED, or any other layout.
		vk::AccessFlags srcAccess = {};
		vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		if (prevLayout != vk::ImageLayout::eUndefined)
		{
			srcAccess = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
			srcStage = vk::PipelineStageFlagBits::eAllCommands;
		}

		vk::ImageMemoryBarrier barrier(
			srcAccess,
			vk::AccessFlagBits::eTransferRead,
			prevLayout,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			image,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBufs[0].pipelineBarrier(srcStage,
		                           vk::PipelineStageFlagBits::eTransfer,
		                           {}, {}, {}, barrier);

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();

		// Update our CPU-side layout tracking
		vulkanImage.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
	}

	// --- 2. Read image data to buffer ---
	std::vector<uint8_t> rawData = readImageToBuffer(device, physicalDevice,
	                                                  queue, queueFamilyIndex,
	                                                  image, format, extent,
	                                                  vk::ImageLayout::eTransferSrcOptimal);

	// --- 3. Transition back to the original layout ---
	// eUndefined is NOT a valid newLayout per Vulkan spec.  For images that
	// were never written (SSAO, SSR — still in UNDEFINED), just leave them in
	// TRANSFER_SRC_OPTIMAL; no pass accesses them so the layout is harmless.
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
		constexpr vk::PipelineStageFlags dstStage =
			vk::PipelineStageFlagBits::eAllCommands;

		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eTransferRead,
			dstAccess,
			vk::ImageLayout::eTransferSrcOptimal,
			prevLayout,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			image,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                           dstStage,
		                           {}, {}, {}, barrier);

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();

		// Update CPU-side layout tracking to reflect the restored layout
		vulkanImage.SetCurrentLayout(prevLayout);
	}

	if (rawData.empty())
	{
		return false;
	}

	// --- 4. Format conversion ---
	const uint32_t channels = channelCount(format);
	std::vector<uint8_t> pngData;

	if (format == vk::Format::eR16G16B16A16Sfloat ||
	    format == vk::Format::eR16G16B16A16Unorm ||
	    format == vk::Format::eR16G16B16A16Snorm)
	{
		// Convert half-float → u8
		pngData = convertHalfToU8(rawData.data(), extent.width, extent.height, remapSigned);
	}
	else
	{
		// Direct copy for 8-bit formats
		pngData = std::move(rawData);

		// Swizzle BGR → RGB if needed
		if (isBGRFormat(format) && channels >= 3)
		{
			swizzleBGRtoRGB(pngData.data(), extent.width, extent.height, channels);
		}
	}

	// --- 5. Write PNG ---
	ensureDirectory(path);

	const int stride = static_cast<int>(extent.width) * static_cast<int>(channels);
	const int writeResult = stbi_write_png(path.c_str(),
	                                       static_cast<int>(extent.width),
	                                       static_cast<int>(extent.height),
	                                       static_cast<int>(channels),
	                                       pngData.data(),
	                                       stride);
	return writeResult != 0;
}

// ===========================================================================
// CaptureAllAttachments
// ===========================================================================

int Screenshot::CaptureAllAttachments(const vk::raii::Device& device,
                                       const vk::raii::PhysicalDevice& physicalDevice,
                                       vk::Queue queue,
                                       uint32_t queueFamilyIndex,
                                       AttachmentManager& attachmentManager,
                                       const std::string& prefix)
{
	// All known attachment names (excluding Depth which can't be read back as color)
	static constexpr AttachmentName kAttachmentNames[] = {
		AttachmentName::Position,
		AttachmentName::Normal,
		AttachmentName::Albedo,
		AttachmentName::MetallicRoughness,
		AttachmentName::HDRColor,
		AttachmentName::SSAO,
		AttachmentName::SSR,
	};

	int capturedCount = 0;

	for (const auto name : kAttachmentNames)
	{
		if (!attachmentManager.HasAttachment(name))
		{
			continue;
		}

		VulkanImage& image = attachmentManager.GetAttachment(name);
		const std::string fileName = timestampedFilename(
			prefix + "_" + AttachmentNameToString(name), ".png");

		const bool remapSigned = (name == AttachmentName::Normal);
		bool succeed = CaptureAttachment(device, physicalDevice, queue, queueFamilyIndex,
			image, fileName, remapSigned);
		if (succeed)
		{
			++capturedCount;
		}
	}

	return capturedCount;
}

} // namespace neurus
