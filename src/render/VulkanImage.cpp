#include "VulkanImage.h"

#include "Log.h"

#include <stdexcept>
#include <algorithm>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

VulkanImage::VulkanImage(const vk::raii::Device& device,
                         const vk::raii::PhysicalDevice& physicalDevice,
                         const vk::Extent2D extent,
                         const vk::Format format,
                         const vk::ImageUsageFlags usage,
                         const uint32_t mipLevels,
                         const ImageType imageType,
                         const char* debugName)
	: m_extent(extent)
	, m_format(format)
	, m_usage(usage)
	, m_mipLevels(mipLevels)
	, m_imageType(imageType)
	, m_currentLayout(vk::ImageLayout::eUndefined)
{
	createImage(device, physicalDevice);
	allocateAndBindMemory(device, physicalDevice);
	createImageView(device);

	// --- Set debug name in Debug builds ---
#ifdef _DEBUG
	if (debugName && *debugName)
	{
		const vk::DebugUtilsObjectNameInfoEXT nameInfo(
			vk::ObjectType::eImage,
			reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_image)),
			debugName);
		device.setDebugUtilsObjectNameEXT(nameInfo);
	}
#endif

	{
		const char* typeStr = (m_imageType == ImageType::e2D) ? "2D" :
		                      (m_imageType == ImageType::eCube) ? "Cube" :
		                      (m_imageType == ImageType::eDepthStencil) ? "DepthStencil" : "Unknown";
		NEURUS_LOG("[VulkanImage] " << m_extent.width << "x" << m_extent.height
		          << " mips=" << m_mipLevels
		          << " type=" << typeStr
		          << " format=" << vk::to_string(m_format)
		          << " usage=" << vk::to_string(m_usage)
		          << " handle=0x" << std::hex << reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_image)) << std::dec
		          << (debugName ? " name='" : "")
		          << (debugName ? debugName : "")
		          << (debugName ? "'" : ""));
	}
}

// ---------------------------------------------------------------------------
// Image creation
// ---------------------------------------------------------------------------

void VulkanImage::createImage(const vk::raii::Device& device,
                              const vk::raii::PhysicalDevice& /*physicalDevice*/)
{
	vk::ImageCreateFlags createFlags;

	switch (m_imageType)
	{
	case ImageType::eCube:
		m_arrayLayers = 6;
		createFlags = vk::ImageCreateFlagBits::eCubeCompatible;
		break;
	case ImageType::eDepthStencil:
		m_arrayLayers = 1;
		createFlags = {};
		break;
	case ImageType::e2D:
	default:
		m_arrayLayers = 1;
		createFlags = {};
		break;
	}

	const vk::Extent3D extent3D(m_extent.width, m_extent.height, 1);
	const vk::ImageCreateInfo imageCI(
		createFlags,
		vk::ImageType::e2D,
		m_format,
		extent3D,
		m_mipLevels,
		m_arrayLayers,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		m_usage,
		vk::SharingMode::eExclusive,
		{},
		vk::ImageLayout::eUndefined);

	m_image = vk::raii::Image(device, imageCI);
}

// ---------------------------------------------------------------------------
// Memory allocation & binding
// ---------------------------------------------------------------------------

void VulkanImage::allocateAndBindMemory(const vk::raii::Device& device,
                                        const vk::raii::PhysicalDevice& physicalDevice)
{
	const auto memReqs = m_image.getMemoryRequirements();
	const auto typeIndex = FindMemoryType(physicalDevice,
	                                      memReqs.memoryTypeBits,
	                                      vk::MemoryPropertyFlagBits::eDeviceLocal);

	const vk::MemoryAllocateInfo allocInfo(memReqs.size, typeIndex);
	m_deviceMemory = vk::raii::DeviceMemory(device, allocInfo);

	m_image.bindMemory(*m_deviceMemory, 0);
}

// ---------------------------------------------------------------------------
// Image view creation
// ---------------------------------------------------------------------------

void VulkanImage::createImageView(const vk::raii::Device& device)
{
	vk::ImageViewType viewType;
	vk::ImageAspectFlags aspect;

	switch (m_imageType)
	{
	case ImageType::eCube:
		viewType = vk::ImageViewType::eCube;
		aspect = vk::ImageAspectFlagBits::eColor;
		break;
	case ImageType::eDepthStencil:
		viewType = vk::ImageViewType::e2D;
		aspect = AspectFromFormat(m_format);
		break;
	case ImageType::e2D:
	default:
		viewType = vk::ImageViewType::e2D;
		aspect = vk::ImageAspectFlagBits::eColor;
		break;
	}

	const vk::ImageSubresourceRange subresourceRange(
		aspect,
		0,               // baseMipLevel
		m_mipLevels,     // levelCount
		0,               // baseArrayLayer
		m_arrayLayers    // layerCount
	);

	const vk::ComponentMapping components; // identity mapping

	const vk::ImageViewCreateInfo viewCI(
		{},
		*m_image,
		viewType,
		m_format,
		components,
		subresourceRange);

	m_imageView = vk::raii::ImageView(device, viewCI);
}

// ---------------------------------------------------------------------------
// Layout transition
// ---------------------------------------------------------------------------

void VulkanImage::TransitionLayout(const vk::raii::CommandBuffer& cmdBuf,
                                   const vk::ImageLayout oldLayout,
                                   const vk::ImageLayout newLayout,
                                   const uint32_t baseMipLevel,
                                   const uint32_t levelCount,
                                   const uint32_t baseArrayLayer,
                                   const uint32_t layerCount)
{
	const auto aspect = (m_imageType == ImageType::eDepthStencil)
	                        ? AspectFromFormat(m_format)
	                        : vk::ImageAspectFlagBits::eColor;

	const vk::ImageSubresourceRange subresourceRange(
		aspect,
		baseMipLevel,
		levelCount,
		baseArrayLayer,
		layerCount);

	const vk::ImageMemoryBarrier barrier(
		AccessFlagsForLayout(oldLayout),
		AccessFlagsForLayout(newLayout),
		oldLayout,
		newLayout,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		*m_image,
		subresourceRange);

	cmdBuf.pipelineBarrier(
		PipelineStageForLayout(oldLayout),
		PipelineStageForLayout(newLayout),
		{},
		{},
		{},
		{ barrier });

	m_currentLayout = newLayout;
}

// ---------------------------------------------------------------------------
// Mipmap generation via vkCmdBlitImage
// ---------------------------------------------------------------------------

void VulkanImage::GenerateMipmaps(const vk::raii::CommandBuffer& cmdBuf)
{
	if (m_mipLevels <= 1)
	{
		return;
	}

	const auto aspect = (m_imageType == ImageType::eDepthStencil)
	                        ? AspectFromFormat(m_format)
	                        : vk::ImageAspectFlagBits::eColor;

	int32_t mipWidth  = static_cast<int32_t>(m_extent.width);
	int32_t mipHeight = static_cast<int32_t>(m_extent.height);

	for (uint32_t i = 1; i < m_mipLevels; ++i)
	{
		// --- Transition level (i-1) TRANSFER_DST → TRANSFER_SRC ---
		TransitionLayout(cmdBuf,
		                 vk::ImageLayout::eTransferDstOptimal,
		                 vk::ImageLayout::eTransferSrcOptimal,
		                 i - 1, 1,
		                 0, m_arrayLayers);

		const int32_t srcWidth  = mipWidth;
		const int32_t srcHeight = mipHeight;
		const int32_t dstWidth  = std::max(1, mipWidth / 2);
		const int32_t dstHeight = std::max(1, mipHeight / 2);

		vk::ImageBlit blit;
		blit.srcSubresource.aspectMask     = aspect;
		blit.srcSubresource.mipLevel       = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount     = m_arrayLayers;
		blit.srcOffsets[0] = vk::Offset3D(0, 0, 0);
		blit.srcOffsets[1] = vk::Offset3D(srcWidth, srcHeight, 1);

		blit.dstSubresource.aspectMask     = aspect;
		blit.dstSubresource.mipLevel       = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount     = m_arrayLayers;
		blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
		blit.dstOffsets[1] = vk::Offset3D(dstWidth, dstHeight, 1);

		cmdBuf.blitImage(
			*m_image, vk::ImageLayout::eTransferSrcOptimal,
			*m_image, vk::ImageLayout::eTransferDstOptimal,
			{ blit },
			vk::Filter::eLinear);

		// --- Transition level (i-1) TRANSFER_SRC → SHADER_READ_ONLY ---
		TransitionLayout(cmdBuf,
		                 vk::ImageLayout::eTransferSrcOptimal,
		                 vk::ImageLayout::eShaderReadOnlyOptimal,
		                 i - 1, 1,
		                 0, m_arrayLayers);

		mipWidth  = dstWidth;
		mipHeight = dstHeight;
	}

	// --- Transition last level TRANSFER_DST → SHADER_READ_ONLY ---
	TransitionLayout(cmdBuf,
	                 vk::ImageLayout::eTransferDstOptimal,
	                 vk::ImageLayout::eShaderReadOnlyOptimal,
	                 m_mipLevels - 1, 1,
	                 0, m_arrayLayers);
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

vk::AccessFlags VulkanImage::AccessFlagsForLayout(const vk::ImageLayout layout)
{
	switch (layout)
	{
	case vk::ImageLayout::eUndefined:
	case vk::ImageLayout::ePresentSrcKHR:
		return {};
	case vk::ImageLayout::eTransferDstOptimal:
		return vk::AccessFlagBits::eTransferWrite;
	case vk::ImageLayout::eTransferSrcOptimal:
		return vk::AccessFlagBits::eTransferRead;
	case vk::ImageLayout::eColorAttachmentOptimal:
		return vk::AccessFlagBits::eColorAttachmentWrite;
	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		return vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	case vk::ImageLayout::eShaderReadOnlyOptimal:
		return vk::AccessFlagBits::eShaderRead;
	case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
		return vk::AccessFlagBits::eShaderRead;
	case vk::ImageLayout::eGeneral:
		return vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
	default:
		return {};
	}
}

vk::PipelineStageFlags VulkanImage::PipelineStageForLayout(const vk::ImageLayout layout)
{
	switch (layout)
	{
	case vk::ImageLayout::eUndefined:
	case vk::ImageLayout::ePresentSrcKHR:
		return vk::PipelineStageFlagBits::eTopOfPipe;
	case vk::ImageLayout::eTransferDstOptimal:
	case vk::ImageLayout::eTransferSrcOptimal:
		return vk::PipelineStageFlagBits::eTransfer;
	case vk::ImageLayout::eColorAttachmentOptimal:
		return vk::PipelineStageFlagBits::eColorAttachmentOutput;
	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		return vk::PipelineStageFlagBits::eEarlyFragmentTests;
	case vk::ImageLayout::eShaderReadOnlyOptimal:
	case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
		return vk::PipelineStageFlagBits::eFragmentShader;
	case vk::ImageLayout::eGeneral:
		return vk::PipelineStageFlagBits::eAllCommands;
	default:
		return vk::PipelineStageFlagBits::eTopOfPipe;
	}
}

vk::ImageAspectFlags VulkanImage::AspectFromFormat(const vk::Format format)
{
	switch (format)
	{
	// Depth + Stencil formats
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32SfloatS8Uint:
		return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

	// Depth‑only formats
	case vk::Format::eD16Unorm:
	case vk::Format::eD32Sfloat:
	case vk::Format::eX8D24UnormPack32:
		return vk::ImageAspectFlagBits::eDepth;

	// Stencil‑only
	case vk::Format::eS8Uint:
		return vk::ImageAspectFlagBits::eStencil;

	// Default: color
	default:
		return vk::ImageAspectFlagBits::eColor;
	}
}

uint32_t VulkanImage::FindMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
                                     const uint32_t typeFilter,
                                     const vk::MemoryPropertyFlags properties)
{
	const auto memProps = physicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1u << i)) &&
		    (memProps.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	throw std::runtime_error("VulkanImage: failed to find suitable memory type.");
}

} // namespace neurus
