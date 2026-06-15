#include "Swapchain.h"

#include <algorithm>
#include <stdexcept>

namespace neurus {

Swapchain::Swapchain(const vk::raii::PhysicalDevice& physicalDevice,
                     const vk::raii::Device& device,
                     const vk::raii::SurfaceKHR& surface,
                     uint32_t width, uint32_t height)
	: m_physicalDevice(physicalDevice)
	, m_device(device)
	, m_surface(surface)
{
	m_recreateWidth = width;
	m_recreateHeight = height;

	auto surfaceFormat = chooseSurfaceFormat(physicalDevice, surface);
	m_format = surfaceFormat.format;
	auto presentMode = choosePresentMode(physicalDevice, surface);
	m_extent = chooseExtent(physicalDevice, surface, width, height);

	// --- Get surface capabilities for image count ---
	auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
	uint32_t minImageCount = capabilities.minImageCount + 1;

	// Clamp to maxImageCount if it's not unlimited (0 = unlimited)
	if (capabilities.maxImageCount > 0 && minImageCount > capabilities.maxImageCount)
	{
		minImageCount = capabilities.maxImageCount;
	}

	vk::SwapchainCreateInfoKHR swapchainCreateInfo(
		{},
		*surface,
		minImageCount,
		m_format,
		vk::ColorSpaceKHR::eSrgbNonlinear,
		m_extent,
		1,                                  // Single array layer (no stereo rendering)
		vk::ImageUsageFlagBits::eColorAttachment,
		vk::SharingMode::eExclusive,        // Single queue family for MVP
		{},
		capabilities.currentTransform,      // Use surface's actual transform
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		presentMode,
		VK_TRUE,                            // clipped
		nullptr                             // old swapchain (null for initial creation)
	);

	m_swapchain = std::make_unique<vk::raii::SwapchainKHR>(device, swapchainCreateInfo);

	// --- Store actual values ---
	m_extent = swapchainCreateInfo.imageExtent;
	m_imageCount = minImageCount;

	// --- Create image views ---
	m_images = m_swapchain->getImages();
	m_imageViews = createImageViews(device, *m_swapchain, m_format);
}

Swapchain::~Swapchain()
{
	// vk::raii handles cleanup automatically.
	// Image views destroyed first, then swapchain.
	// Order is ensured by C++ destruction order (member reverse order).
}

void Swapchain::Recreate(uint32_t width, uint32_t height)
{
	m_recreateWidth = width;
	m_recreateHeight = height;

	// Destroy old resources (swapchain + image views)
	m_imageViews.clear();
	m_swapchain.reset();

	// Get new surface capabilities (window may have changed)
	auto capabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);

	// Handle minimized window (zero-area surface)
	if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0)
	{
		// Wait until window is restored before recreating
		// The next DrawFrame() call will retry recreation
		return;
	}

	m_extent = chooseExtent(m_physicalDevice, m_surface, width, height);
	auto presentMode = choosePresentMode(m_physicalDevice, m_surface);

	uint32_t minImageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && minImageCount > capabilities.maxImageCount)
	{
		minImageCount = capabilities.maxImageCount;
	}

	vk::SwapchainCreateInfoKHR swapchainCreateInfo(
		{},
		*m_surface,
		minImageCount,
		m_format,
		vk::ColorSpaceKHR::eSrgbNonlinear,
		m_extent,
		1,
		vk::ImageUsageFlagBits::eColorAttachment,
		vk::SharingMode::eExclusive,
		{},
		vk::SurfaceTransformFlagBitsKHR::eIdentity,
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		presentMode,
		VK_TRUE,
		nullptr
	);

	m_swapchain = std::make_unique<vk::raii::SwapchainKHR>(m_device, swapchainCreateInfo);
	m_extent = swapchainCreateInfo.imageExtent;
	m_imageCount = minImageCount;

	m_images = m_swapchain->getImages();
	m_imageViews = createImageViews(m_device, *m_swapchain, m_format);
}

uint32_t Swapchain::AcquireNextImage(const vk::raii::Semaphore& signalSemaphore)
{
	// A timeout of UINT64_MAX disables the timeout
	auto [result, imageIndex] = m_swapchain->acquireNextImage(
		UINT64_MAX, *signalSemaphore, nullptr);

	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
	{
		// Swapchain needs recreation (window resized)
		Recreate(m_recreateWidth, m_recreateHeight);
		// Return placeholder — caller should skip this frame
		return 0;
	}

	if (result != vk::Result::eSuccess)
	{
		throw std::runtime_error("Failed to acquire swapchain image.");
	}

	return imageIndex;
}

void Swapchain::Present(const vk::raii::Semaphore& waitSemaphore, uint32_t imageIndex)
{
	vk::PresentInfoKHR presentInfo(
		*waitSemaphore,
		**m_swapchain,
		imageIndex
	);

	auto result = m_device.getQueue(0, 0).presentKHR(presentInfo);

	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
	{
		Recreate(m_recreateWidth, m_recreateHeight);
	}
}

vk::SurfaceFormatKHR Swapchain::chooseSurfaceFormat(const vk::raii::PhysicalDevice& physicalDevice,
                                                      const vk::raii::SurfaceKHR& surface)
{
	auto formats = physicalDevice.getSurfaceFormatsKHR(*surface);

	// Prefer sRGB BGRA8
	for (const auto& format : formats)
	{
		if (format.format == vk::Format::eB8G8R8A8Srgb &&
		    format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
		{
			return format;
		}
	}

	// Fallback: first available format
	return formats[0];
}

vk::PresentModeKHR Swapchain::choosePresentMode(const vk::raii::PhysicalDevice& physicalDevice,
                                                  const vk::raii::SurfaceKHR& surface)
{
	auto presentModes = physicalDevice.getSurfacePresentModesKHR(*surface);

	// Prefer MAILBOX for low latency (tearing-free, doesn't block on vsync)
	for (const auto& mode : presentModes)
	{
		if (mode == vk::PresentModeKHR::eMailbox)
		{
			return mode;
		}
	}

	// Fallback to FIFO (guaranteed to be available, VSync)
	return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseExtent(const vk::raii::PhysicalDevice& physicalDevice,
                                       const vk::raii::SurfaceKHR& surface,
                                       uint32_t width, uint32_t height)
{
	auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);

	// If the current extent is valid, use it
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}

	// Otherwise clamp to min/max
	vk::Extent2D extent = {width, height};
	extent.width = std::clamp(extent.width,
	                          capabilities.minImageExtent.width,
	                          capabilities.maxImageExtent.width);
	extent.height = std::clamp(extent.height,
	                           capabilities.minImageExtent.height,
	                           capabilities.maxImageExtent.height);
	return extent;
}

std::vector<vk::raii::ImageView> Swapchain::createImageViews(const vk::raii::Device& device,
                                                               const vk::raii::SwapchainKHR& swapchain,
                                                               vk::Format format)
{
	auto images = swapchain.getImages();
	std::vector<vk::raii::ImageView> views;
	views.reserve(images.size());

	for (const auto& image : images)
	{
		vk::ImageViewCreateInfo viewCreateInfo(
			{},
			image,
			vk::ImageViewType::e2D,
			format,
			vk::ComponentMapping(),  // Identity mapping (R→R, G→G, B→B, A→A)
			vk::ImageSubresourceRange(
				vk::ImageAspectFlagBits::eColor,
				0, 1,  // base mip level, level count
				0, 1   // base array layer, layer count
			)
		);

		views.emplace_back(device, viewCreateInfo);
	}

	return views;
}

} // namespace neurus
