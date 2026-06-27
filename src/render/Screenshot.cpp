#include "Screenshot.h"
#include "Image.h"
#include "RenderCache.h"
#include "asset/ImageData.h"
#include "Texture.h"
#include "render/Barrier.h"
#include "Log.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace neurus {

// ===========================================================================
// Timestamp helper
// ===========================================================================

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
	if (ImageData::PixelByteSize(format) == 0)
	{
		return false;
	}

	// --- 1. Transition PRESENT_SRC → TRANSFER_SRC ---
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

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

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();
	}

	// --- 2. Read back via staging buffer ---
	const uint32_t bytesPerPixel = [&]() -> uint32_t {
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
		case vk::Format::eR32G32B32A32Sfloat:
			return 16;
		case vk::Format::eR8Unorm:
		case vk::Format::eR8Srgb:
			return 1;
		default:
			return 0;
		}
	}();

	const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(extent.width) *
	                                 extent.height * bytesPerPixel;

	// --- Staging buffer ---
	vk::BufferCreateInfo stagingCI({}, imageSize, vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuffer(device, stagingCI);

	auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
	uint32_t stagingMemType = [&]() -> uint32_t {
		const auto memProps = physicalDevice.getMemoryProperties();
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((stagingMemReqs.memoryTypeBits & (1u << i)) &&
			    (memProps.memoryTypes[i].propertyFlags &
			     (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) ==
			        (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))
			{
				return i;
			}
		}
		NEURUS_ERR("[Screenshot] Failed to find memory type for staging buffer");
		return 0;
	}();

	vk::MemoryAllocateInfo stagingAlloc(stagingMemReqs.size, stagingMemType);
	vk::raii::DeviceMemory stagingMemory(device, stagingAlloc);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	// --- Transient command buffer ---
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		vk::BufferImageCopy copyRegion;
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
		copyRegion.imageExtent = vk::Extent3D(extent.width, extent.height, 1);

		cmdBufs[0].copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, *stagingBuffer, copyRegion);

		vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite,
		                          vk::AccessFlagBits::eHostRead);
		cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                           vk::PipelineStageFlagBits::eHost,
		                           {}, {barrier}, {}, {});

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();
	}

	std::vector<uint8_t> rawData(static_cast<size_t>(imageSize));
	void* mapped = stagingMemory.mapMemory(0, imageSize);
	std::memcpy(rawData.data(), mapped, static_cast<size_t>(imageSize));
	stagingMemory.unmapMemory();

	// --- 3. Transition back TRANSFER_SRC → PRESENT_SRC ---
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

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

		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();
	}

	// --- 4. Delegate PNG write to ImageData ---
	ImageData imgData(rawData.data(), extent.width, extent.height, format);
	return imgData.SavePNG(path);
}

// ===========================================================================
// CaptureAttachment
// ===========================================================================

bool Screenshot::CaptureAttachment(const vk::raii::Device& device,
                                    const vk::raii::PhysicalDevice& physicalDevice,
                                    vk::Queue queue,
                                    uint32_t queueFamilyIndex,
                                     Image& vulkanImage,
                                    const std::string& path,
                                    bool remapSigned)
{
	if (ImageData::PixelByteSize(vulkanImage.Format()) == 0)
	{
		return false;
	}

	// All GPU operations (layout transitions, readback) and CPU operations
	// (format conversion, PNG write) are handled by Texture::SaveImage.
	return Texture::SaveImage(vulkanImage, device, physicalDevice,
	                               queue, queueFamilyIndex, path, remapSigned);
}

// ===========================================================================
// CaptureAllAttachments
// ===========================================================================

int Screenshot::CaptureAllAttachments(const vk::raii::Device& device,
                                       const vk::raii::PhysicalDevice& physicalDevice,
                                       vk::Queue queue,
                                       uint32_t queueFamilyIndex,
                                       RenderCache& renderCache,
                                       vk::Extent2D extent,
                                       const std::string& prefix)
{
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
		if (!renderCache.HasAttachment(name))
		{
			continue;
		}

		Image& image = renderCache.GetAttachment(name, extent);

		// Skip attachments that have never been written (current layout UNDEFINED).
		// Capturing them would leave them in TRANSFER_SRC_OPTIMAL, causing
		// validation errors when a subsequent render pass expects a usable layout.
		if (image.State() == ImageState::Undefined)
		{
			NEURUS_LOG("[Screenshot] Skipping " << AttachmentNameToString(name)
			           << " - layout is UNDEFINED (not yet written)");
			continue;
		}

		const std::string fileName = timestampedFilename(
			prefix + "_" + AttachmentNameToString(name), ".png");

		const bool remapSigned = (name == AttachmentName::Normal);
		if (CaptureAttachment(device, physicalDevice, queue, queueFamilyIndex,
		                      image, fileName, remapSigned))
		{
			++capturedCount;
		}
	}

	return capturedCount;
}

} // namespace neurus
