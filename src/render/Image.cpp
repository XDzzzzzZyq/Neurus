#include "Image.h"
#include "Barrier.h"

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
             const bool arrayView)
	: m_extent(extent)
	, m_format(format)
	, m_usage(usage)
	, m_mipLevels(mipLevels)
	, m_imageType(imageType)
	, m_arrayView(arrayView)
{
	createImage(device, physicalDevice);
	allocateAndBindMemory(device, physicalDevice);
	createImageView(device, debugName);

	if (m_imageType == ImageType::eCube)
	{
		createFaceViews(device);
		createMultiviewView(device);
	}

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
// Static factory: FromImageData
// ---------------------------------------------------------------------------

std::unique_ptr<Image> Image::FromImageData(const vk::raii::Device& device,
                                            const vk::raii::PhysicalDevice& physicalDevice,
                                            vk::Queue queue,
                                            uint32_t queueFamilyIndex,
                                            ImageData& imageData,
                                            const char* debugName,
                                            vk::ImageUsageFlags extraUsage)
{
	if (!imageData.IsValid())
	{
		NEURUS_ERR("[Image] FromImageData: invalid ImageData provided");
		return nullptr;
	}

	auto image = std::make_unique<Image>(
		device, physicalDevice,
		vk::Extent2D{imageData.GetWidth(), imageData.GetHeight()},
		imageData.GetFormat(),
		vk::ImageUsageFlagBits::eSampled |
		    vk::ImageUsageFlagBits::eTransferDst |
		    extraUsage,
		1,
		ImageType::e2D,
		debugName);

	image->UploadImageData(device, physicalDevice, queue, queueFamilyIndex, imageData);

	return image;
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
		aspect = AspectFromFormat(m_format);
		break;
	case ImageType::eDepthStencil:
		viewType = vk::ImageViewType::e2D;
		aspect = AspectFromFormat(m_format);
		break;
	case ImageType::e2D:
	default:
		viewType = m_arrayView ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
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
// Mipmap generation via vkCmdBlitImage
// ---------------------------------------------------------------------------

void Image::GenerateMipmaps(const vk::raii::CommandBuffer& cmdBuf)
{
	if (m_mipLevels <= 1)
	{
		return;
	}

	const auto aspect = AspectFromFormat(m_format);

	int32_t mipWidth  = static_cast<int32_t>(m_extent.width);
	int32_t mipHeight = static_cast<int32_t>(m_extent.height);

	for (uint32_t i = 1; i < m_mipLevels; ++i)
	{
		// At this point, level (i-1) is guaranteed to be in TransferDst:
		//   - Iteration 1: set by the initial full-image barrier before GenerateMipmaps
		//   - Iteration N: level (i-1) was the blit destination in iteration (N-1)
		// We reset m_state so Barrier::Transition reads the correct "old" layout.

		// --- Transition level (i-1) TransferDst → TransferSrc ---
		m_state = ImageState::TransferDst;
		{
			vk::ImageSubresourceRange range(aspect, i - 1, 1, 0, m_arrayLayers);
			Barrier::Transition(*cmdBuf, *this, ImageState::TransferSrc, range);
		}

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

		auto vulkanSrcState = Barrier::ToVulkanImageState(ImageState::TransferSrc);
		auto vulkanDstState = Barrier::ToVulkanImageState(ImageState::TransferDst);

		cmdBuf.blitImage(
			*m_image, vulkanSrcState.layout,
			*m_image, vulkanDstState.layout,
			{ blit },
			vk::Filter::eLinear);

		// --- Transition level (i-1) TransferSrc → ShaderRead ---
		m_state = ImageState::TransferSrc;
		{
			vk::ImageSubresourceRange range(aspect, i - 1, 1, 0, m_arrayLayers);
			Barrier::Transition(*cmdBuf, *this, ImageState::ColorShaderRead, range);
		}

		mipWidth  = dstWidth;
		mipHeight = dstHeight;
	}

	// --- Transition last mip level TransferDst → ShaderRead ---
	m_state = ImageState::TransferDst;
	{
		vk::ImageSubresourceRange lastRange(aspect, m_mipLevels - 1, 1, 0, m_arrayLayers);
		Barrier::Transition(*cmdBuf, *this, ImageState::ColorShaderRead, lastRange);
	}

	// Update CPU-side state tracking to reflect the final layout
	m_state = ImageState::ColorShaderRead;
}

// ---------------------------------------------------------------------------
// CPU → GPU upload
// ---------------------------------------------------------------------------

void Image::UploadImageData(const vk::raii::Device& device,
                            const vk::raii::PhysicalDevice& physicalDevice,
                            vk::Queue queue,
                            uint32_t queueFamilyIndex,
                            const ImageData& imageData)
{
	const auto& pixelData = imageData.GetPixelData();
	const vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(pixelData.size());

	// --- Create staging buffer ---
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
	std::memcpy(mapped, pixelData.data(), static_cast<size_t>(bufferSize));
	stagingMemory.unmapMemory();

	// --- Transient command buffer ---
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
	                                 queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, poolCI);
	vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocInfo);

	cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	// --- Transition image Undefined → TransferDst ---
	Barrier::Transition(*cmdBufs[0], *this, ImageState::TransferDst);

	// --- Copy buffer → image ---
	{
		vk::BufferImageCopy copyRegion;
		copyRegion.bufferOffset      = 0;
		copyRegion.bufferRowLength   = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
			AspectFromFormat(m_format), 0, 0, m_arrayLayers);
		copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
		copyRegion.imageExtent = vk::Extent3D(m_extent.width, m_extent.height, 1);

		cmdBufs[0].copyBufferToImage(*stagingBuffer, *m_image,
		                             vk::ImageLayout::eTransferDstOptimal,
		                             { copyRegion });
	}

	// --- Transition mip level 0 TransferDst → ShaderRead (only the uploaded mip) ---
	{
		vk::ImageSubresourceRange mip0Range(AspectFromFormat(m_format), 0, 1, 0, m_arrayLayers);
		Barrier::Transition(*cmdBufs[0], *this, ImageState::ColorShaderRead, mip0Range);
	}

	// Update CPU-side state tracking (other mips stay in TransferDst for mipmap generation)
	m_state = ImageState::ColorShaderRead;

	cmdBufs[0].end();

	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
	queue.submit(submitInfo);
	queue.waitIdle();
}

// ---------------------------------------------------------------------------
// GPU readback
// ---------------------------------------------------------------------------

ImageData Image::ReadImageData(const vk::raii::Device& device,
                                const vk::raii::PhysicalDevice& physicalDevice,
                                vk::Queue queue,
                                uint32_t queueFamilyIndex,
                                const vk::ImageSubresourceRange* subresourceRange,
                                vk::Extent2D readExtent)
{
	const uint32_t bytesPerPixel = ImageData::PixelByteSize(m_format);

	if (bytesPerPixel == 0)
	{
		NEURUS_ERR("[Image] ReadImageData: unsupported format " << vk::to_string(m_format));
		return ImageData();
	}

	// Extent to read: explicit override or full image
	const vk::Extent2D copyExtent = (readExtent.width == 0 && readExtent.height == 0)
		? m_extent
		: readExtent;

	// Determine what to read (default: mip 0, layer 0)
	const auto range = subresourceRange
		? *subresourceRange
		: vk::ImageSubresourceRange(AspectFromFormat(m_format), 0, 1, 0, 1);

	const uint32_t layerCount = range.layerCount;
	const uint32_t baseLayer  = range.baseArrayLayer;

	const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(copyExtent.width) *
	                                 copyExtent.height * bytesPerPixel * layerCount;

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

	// --- Transition the requested subresource range to TransferSrc ---
	Barrier::Transition(*cmdBufs[0], *this, ImageState::TransferSrc, range);

	// --- Copy image to buffer ---
	vk::BufferImageCopy copyRegion;
	copyRegion.bufferOffset      = 0;
	copyRegion.bufferRowLength   = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
		AspectFromFormat(m_format), range.baseMipLevel, baseLayer, layerCount);
	copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
	copyRegion.imageExtent = vk::Extent3D(copyExtent.width, copyExtent.height, 1);

	cmdBufs[0].copyImageToBuffer(*m_image, vk::ImageLayout::eTransferSrcOptimal,
	                             *stagingBuffer, { copyRegion });

	vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite,
	                          vk::AccessFlagBits::eHostRead);
	cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                           vk::PipelineStageFlagBits::eHost,
	                           {}, {barrier}, {}, {});

	cmdBufs[0].end();

	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
	queue.submit(submitInfo);
	queue.waitIdle();

	void* mapped = stagingMemory.mapMemory(0, imageSize);
	ImageData result(mapped, copyExtent.width, copyExtent.height, m_format, layerCount);
	stagingMemory.unmapMemory();

	return result;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

vk::ImageAspectFlags Image::AspectFromFormat(const vk::Format format)
{
	switch (format)
	{
	// Depth + Stencil formats
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32SfloatS8Uint:
		return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

	// Depth-only formats
	case vk::Format::eD16Unorm:
	case vk::Format::eD32Sfloat:
	case vk::Format::eX8D24UnormPack32:
		return vk::ImageAspectFlagBits::eDepth;

	// Stencil-only
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

// ---------------------------------------------------------------------------
// Cube-only views
// ---------------------------------------------------------------------------

const vk::raii::ImageView& Image::FaceView(uint32_t faceIdx) const
{
	return m_faceViews[faceIdx];
}

const vk::raii::ImageView& Image::ArrayView() const
{
	return m_multiviewView;
}

void Image::createFaceViews(const vk::raii::Device& device)
{
	const auto aspect = AspectFromFormat(m_format);
	m_faceViews.clear();
	m_faceViews.reserve(6);
	for (uint32_t f = 0; f < 6; ++f)
	{
		vk::ImageViewCreateInfo ci({}, *m_image,
			vk::ImageViewType::e2D, m_format,
			vk::ComponentMapping(),
			vk::ImageSubresourceRange(aspect, 0, 1, f, 1));
		m_faceViews.emplace_back(device, ci);
	}
}

void Image::createMultiviewView(const vk::raii::Device& device)
{
	const auto aspect = AspectFromFormat(m_format);
	vk::ImageViewCreateInfo ci({}, *m_image,
		vk::ImageViewType::e2DArray, m_format,
		vk::ComponentMapping(),
		vk::ImageSubresourceRange(aspect, 0, 1, 0, 6));
	m_multiviewView = vk::raii::ImageView(device, ci);
}

// ---------------------------------------------------------------------------
// Subresource range helpers
// ---------------------------------------------------------------------------

vk::ImageSubresourceRange Image::AllSubresources() const
{
	return { AspectFromFormat(m_format), 0, m_mipLevels, 0, m_arrayLayers };
}

vk::ImageSubresourceRange Image::Mip(uint32_t level) const
{
	return { AspectFromFormat(m_format), level, 1, 0, m_arrayLayers };
}

vk::ImageSubresourceRange Image::Mips(uint32_t base, uint32_t count) const
{
	return { AspectFromFormat(m_format), base, count, 0, m_arrayLayers };
}

vk::ImageSubresourceRange Image::Layer(uint32_t layer) const
{
	return { AspectFromFormat(m_format), 0, m_mipLevels, layer, 1 };
}

vk::ImageSubresourceRange Image::Layers(uint32_t base, uint32_t count) const
{
	return { AspectFromFormat(m_format), 0, m_mipLevels, base, count };
}

} // namespace neurus
