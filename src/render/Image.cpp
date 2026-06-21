#include "Image.h"

#include "Log.h"

#include <stdexcept>
#include <algorithm>
#include <string>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Image::Image(const vk::raii::Device& device,
             const vk::raii::PhysicalDevice& physicalDevice,
             const vk::Extent2D extent,
             const vk::Format format,
             const vk::ImageUsageFlags usage,
             const uint32_t mipLevels,
             const ImageType imageType,
             const char* debugName,
             const ImageData& imageData)
	: m_extent(extent)
	, m_format(format)
	, m_usage(usage)
	, m_mipLevels(mipLevels)
	, m_imageType(imageType)
	, m_imageData(imageData)
	, m_currentLayout(vk::ImageLayout::eUndefined)
{
	createImage(device, physicalDevice);
	allocateAndBindMemory(device, physicalDevice);
	createImageView(device, debugName);

	// --- Set debug name in Debug builds ---
#ifdef _DEBUG
	if (debugName && *debugName)
	{
		const vk::DebugUtilsObjectNameInfoEXT nameInfo(
			vk::ObjectType::eImage,
			reinterpret_cast<uint64_t>(static_cast<VkImage>(*m_image)),
			debugName);
		device.setDebugUtilsObjectNameEXT(nameInfo);

		// Name the device memory
		{
			std::string memName(debugName);
			memName += "_Mem";
			vk::DebugUtilsObjectNameInfoEXT memNameInfo(
				vk::ObjectType::eDeviceMemory,
				reinterpret_cast<uint64_t>(static_cast<VkDeviceMemory>(*m_deviceMemory)),
				memName.c_str());
			device.setDebugUtilsObjectNameEXT(memNameInfo);
		}
	}
#endif

	{
		const char* typeStr = (m_imageType == ImageType::e2D) ? "2D" :
		                      (m_imageType == ImageType::eCube) ? "Cube" :
		                      (m_imageType == ImageType::eDepthStencil) ? "DepthStencil" : "Unknown";
		NEURUS_LOG("[Image] " << m_extent.width << "x" << m_extent.height
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

void Image::createImage(const vk::raii::Device& device,
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

void Image::allocateAndBindMemory(const vk::raii::Device& device,
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

void Image::createImageView(const vk::raii::Device& device, const char* debugName)
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

	// --- Set debug name on image view in Debug builds ---
#ifdef _DEBUG
	if (debugName && *debugName)
	{
		std::string viewName(debugName);
		viewName += "_View";
		vk::DebugUtilsObjectNameInfoEXT nameInfo(
			vk::ObjectType::eImageView,
			reinterpret_cast<uint64_t>(static_cast<VkImageView>(*m_imageView)),
			viewName.c_str());
		device.setDebugUtilsObjectNameEXT(nameInfo);
	}
#endif
}

// ---------------------------------------------------------------------------
// Layout transition
// ---------------------------------------------------------------------------

void Image::TransitionLayout(const vk::raii::CommandBuffer& cmdBuf,
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

void Image::GenerateMipmaps(const vk::raii::CommandBuffer& cmdBuf)
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
// CPU → GPU upload
// ---------------------------------------------------------------------------

void Image::UploadPixelData(const vk::raii::Device& device,
                            const vk::raii::PhysicalDevice& physicalDevice,
                            vk::Queue queue,
                            uint32_t queueFamilyIndex,
                            const void* pixelData,
                            size_t dataSize)
{
	// --- Create staging buffer ---
	const vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(dataSize);
	vk::BufferCreateInfo stagingCI({}, bufferSize, vk::BufferUsageFlagBits::eTransferSrc);
	vk::raii::Buffer stagingBuffer(device, stagingCI);

	auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
	uint32_t stagingMemType = FindMemoryType(physicalDevice,
	                                         stagingMemReqs.memoryTypeBits,
	                                         vk::MemoryPropertyFlagBits::eHostVisible |
	                                         vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::MemoryAllocateInfo stagingAlloc(stagingMemReqs.size, stagingMemType);
	vk::raii::DeviceMemory stagingMemory(device, stagingAlloc);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	// --- Copy pixel data to staging buffer ---
	void* mapped = stagingMemory.mapMemory(0, bufferSize);
	std::memcpy(mapped, pixelData, dataSize);
	stagingMemory.unmapMemory();

	// --- Transient command buffer ---
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
	                                 queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, poolCI);
	vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocInfo);

	cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	// --- Transition image UNDEFINED → TRANSFER_DST_OPTIMAL ---
	{
		const vk::ImageMemoryBarrier barrier(
			{},                                                       // srcAccessMask
			vk::AccessFlagBits::eTransferWrite,                       // dstAccessMask
			vk::ImageLayout::eUndefined,                              // oldLayout
			vk::ImageLayout::eTransferDstOptimal,                     // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*m_image,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, m_arrayLayers));

		cmdBufs[0].pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eTransfer,
			{},
			{},
			{},
			{ barrier });
	}

	// --- Copy buffer → image ---
	{
		vk::BufferImageCopy copyRegion;
		copyRegion.bufferOffset      = 0;
		copyRegion.bufferRowLength   = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
			vk::ImageAspectFlagBits::eColor, 0, 0, m_arrayLayers);
		copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
		copyRegion.imageExtent = vk::Extent3D(m_extent.width, m_extent.height, 1);

		cmdBufs[0].copyBufferToImage(*stagingBuffer, *m_image,
		                             vk::ImageLayout::eTransferDstOptimal,
		                             { copyRegion });
	}

	// --- Transition image TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL ---
	{
		const vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eTransferWrite,                       // srcAccessMask
			vk::AccessFlagBits::eShaderRead,                           // dstAccessMask
			vk::ImageLayout::eTransferDstOptimal,                      // oldLayout
			vk::ImageLayout::eShaderReadOnlyOptimal,                  // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*m_image,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, m_arrayLayers));

		cmdBufs[0].pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader,
			{},
			{},
			{},
			{ barrier });
	}

	cmdBufs[0].end();

	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
	queue.submit(submitInfo);
	queue.waitIdle();

	m_currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

// ---------------------------------------------------------------------------
// Static factory: LoadFromPath
// ---------------------------------------------------------------------------

std::unique_ptr<Image> Image::LoadFromPath(const vk::raii::Device& device,
                                           const vk::raii::PhysicalDevice& physicalDevice,
                                           vk::Queue queue,
                                           uint32_t queueFamilyIndex,
                                           const std::string& path,
                                           const char* debugName)
{
	auto result = ImageData::LoadFromPath(path);
	if (!result.valid())
	{
		return nullptr;
	}

	auto image = std::make_unique<Image>(
		device, physicalDevice,
		vk::Extent2D{result.width, result.height},
		result.format,
		vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
		1,
		ImageType::e2D,
		debugName);

	image->UploadPixelData(device, physicalDevice, queue, queueFamilyIndex,
	                       result.pixelData.data(), result.pixelData.size());

	return image;
}

// ---------------------------------------------------------------------------
// GPU readback
// ---------------------------------------------------------------------------

std::vector<uint8_t> Image::ReadImageToBuffer(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue queue,
	uint32_t queueFamilyIndex,
	vk::ImageLayout currentLayout) const
{
	// Delegate to the static overload using our own image handle, format and extent.
	return ReadImageToBuffer(device, physicalDevice, queue, queueFamilyIndex,
	                         *m_image, m_format, m_extent, currentLayout);
}

// ---------------------------------------------------------------------------
// Static ReadImageToBuffer (raw VkImage overload)
// ---------------------------------------------------------------------------

std::vector<uint8_t> Image::ReadImageToBuffer(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue queue,
	uint32_t queueFamilyIndex,
	vk::Image image,
	vk::Format format,
	vk::Extent2D extent,
	vk::ImageLayout currentLayout)
{
	// Shared implementation - refactor member version to delegate here later.
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
		case vk::Format::eR8Unorm:
		case vk::Format::eR8Srgb:
			return 1;
		default:
			return 0;
		}
	}();

	if (bytesPerPixel == 0) return {};

	const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(extent.width) *
	                                 extent.height * bytesPerPixel;

	// --- Staging buffer ---
	vk::BufferCreateInfo stagingCI({}, imageSize, vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuffer(device, stagingCI);

	auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
	uint32_t stagingMemType = FindMemoryType(physicalDevice,
	                                         stagingMemReqs.memoryTypeBits,
	                                         vk::MemoryPropertyFlagBits::eHostVisible |
	                                         vk::MemoryPropertyFlagBits::eHostCoherent);
	vk::MemoryAllocateInfo stagingAlloc(stagingMemReqs.size, stagingMemType);
	vk::raii::DeviceMemory stagingMemory(device, stagingAlloc);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	// --- Transient command buffer ---
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

	cmdBufs[0].copyImageToBuffer(image, currentLayout, *stagingBuffer, copyRegion);

	vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite,
	                          vk::AccessFlagBits::eHostRead);
	cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                           vk::PipelineStageFlagBits::eHost,
	                           {}, {barrier}, {}, {});

	cmdBufs[0].end();

	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
	queue.submit(submitInfo);
	queue.waitIdle();

	std::vector<uint8_t> pixelData(static_cast<size_t>(imageSize));
	void* mapped = stagingMemory.mapMemory(0, imageSize);
	std::memcpy(pixelData.data(), mapped, static_cast<size_t>(imageSize));
	stagingMemory.unmapMemory();

	return pixelData;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

vk::AccessFlags Image::AccessFlagsForLayout(const vk::ImageLayout layout)
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

vk::PipelineStageFlags Image::PipelineStageForLayout(const vk::ImageLayout layout)
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

vk::ImageAspectFlags Image::AspectFromFormat(const vk::Format format)
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

uint32_t Image::FindMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
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

	throw std::runtime_error("Image: failed to find suitable memory type.");
}

} // namespace neurus
