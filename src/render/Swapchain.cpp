#include "Swapchain.h"

#include "Log.h"

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

	// --- Get surface capabilities for image count and supported usage ---
	auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
	uint32_t minImageCount = capabilities.minImageCount + 1;

	// Clamp to maxImageCount if it's not unlimited (0 = unlimited)
	if (capabilities.maxImageCount > 0 && minImageCount > capabilities.maxImageCount)
	{
		minImageCount = capabilities.maxImageCount;
	}

	// Determine actual image usage: intersect requested usage with surface-supported usage
	constexpr vk::ImageUsageFlags kRequestedUsage =
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	m_actualUsage = kRequestedUsage & capabilities.supportedUsageFlags;

	vk::SwapchainCreateInfoKHR swapchainCreateInfo(
		{},
		*surface,
		minImageCount,
		m_format,
		vk::ColorSpaceKHR::eSrgbNonlinear,
		m_extent,
		1,                                  // Single array layer (no stereo rendering)
		m_actualUsage,
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

	NEURUS_LOG("[Swapchain] " << m_extent.width << "x" << m_extent.height
	          << " format=" << vk::to_string(m_format)
	          << " present=" << vk::to_string(presentMode)
	          << " images=" << m_imageCount
	          << " usage=" << vk::to_string(m_actualUsage));

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

	// Get new surface capabilities (window may have changed).
	// Query surface validity BEFORE destroying the old swapchain so that
	// a lost surface doesn't leave us with a null m_swapchain.
	auto capabilities = vk::SurfaceCapabilitiesKHR{};
	try
	{
		capabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
	}
	catch (const vk::SurfaceLostKHRError&)
	{
		NEURUS_LOG("[Swapchain] Surface lost during recreation - deferring.");
		return;  // Keep the old swapchain - retry next frame
	}

	// Handle minimized window (zero-area surface)
	if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0)
	{
		// Wait until window is restored before recreating
		// The next DrawFrame() call will retry recreation
		return;
	}

	// Destroy old resources **after** confirming the surface is still valid
	m_imageViews.clear();
	m_swapchain.reset();

	m_extent = chooseExtent(m_physicalDevice, m_surface, width, height);
	auto presentMode = choosePresentMode(m_physicalDevice, m_surface);

	uint32_t minImageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && minImageCount > capabilities.maxImageCount)
	{
		minImageCount = capabilities.maxImageCount;
	}

	// Determine actual image usage: intersect requested usage with surface-supported usage
	constexpr vk::ImageUsageFlags kRequestedUsage =
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	m_actualUsage = kRequestedUsage & capabilities.supportedUsageFlags;

	vk::SwapchainCreateInfoKHR swapchainCreateInfo(
		{},
		*m_surface,
		minImageCount,
		m_format,
		vk::ColorSpaceKHR::eSrgbNonlinear,
		m_extent,
		1,
		m_actualUsage,
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

	++m_generation;
}

uint32_t Swapchain::AcquireNextImage(const vk::raii::Semaphore& signalSemaphore)
{
	try
	{
		// UINT64_MAX disables the timeout - the call blocks until an image is
		// available. This is safe because waitForFences (in DrawFrame) uses a
		// finite timeout and processEvents() keeps the Qt event loop responsive.
		auto [result, imageIndex] = m_swapchain->acquireNextImage(
			UINT64_MAX, *signalSemaphore, nullptr);

		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		{
			// Swapchain needs recreation (window resized)
			Recreate(m_recreateWidth, m_recreateHeight);
			return 0;
		}

		if (result != vk::Result::eSuccess)
		{
			throw std::runtime_error("Failed to acquire swapchain image.");
		}

		return imageIndex;
	}
	catch (const vk::OutOfDateKHRError&)
	{
		// vk::raii wrapper may throw instead of returning the error result
		Recreate(m_recreateWidth, m_recreateHeight);
		return 0;
	}
	catch (const vk::SurfaceLostKHRError&)
	{
		// Surface lost - can't acquire. Keep the old swapchain, signal caller.
		return 0;
	}
}

void Swapchain::Present(const vk::raii::Semaphore& waitSemaphore, uint32_t imageIndex, vk::Queue presentQueue)
{
	try
	{
		vk::PresentInfoKHR presentInfo(
			*waitSemaphore,
			**m_swapchain,
			imageIndex
		);

		auto result = presentQueue.presentKHR(presentInfo);

		if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
		{
			Recreate(m_recreateWidth, m_recreateHeight);
		}
	}
	catch (const vk::OutOfDateKHRError&)
	{
		Recreate(m_recreateWidth, m_recreateHeight);
	}
	catch (const vk::SurfaceLostKHRError&)
	{
		// Surface lost - can't present. Don't recreate (would fail).
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
