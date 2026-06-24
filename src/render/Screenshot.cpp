#include "Screenshot.h"
#include "Image.h"
#include "passes/RenderCache.h"
#include "asset/ImageData.h"
#include "Texture.h"
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

	// --- 2. Read back via Image static helper ---
	std::vector<uint8_t> rawData = Image::ReadImageToBuffer(
		device, physicalDevice, queue, queueFamilyIndex,
		image, format, extent, vk::ImageLayout::eTransferSrcOptimal);

	if (rawData.empty())
	{
		return false;
	}

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
	return ImageData::SavePixelData(rawData.data(), format, extent, path);
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
		AttachmentName::ShadowIntensity,
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
		if (image.CurrentLayout() == vk::ImageLayout::eUndefined)
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
