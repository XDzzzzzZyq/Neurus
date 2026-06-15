#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <vector>
#include <memory>

namespace neurus {

/**
 * @brief Manages the Vulkan swapchain and its image views.
 *
 * Wraps vk::raii::SwapchainKHR with support for recreation on window resize.
 * Owns the swapchain handle and all associated image views.
 */
class Swapchain
{
public:
	/**
	 * @brief Creates a swapchain for the given surface and device.
	 * @param physicalDevice Used for querying surface capabilities.
	 * @param device Logical device that owns the swapchain.
	 * @param surface Target surface for presentation.
	 * @param width Desired swapchain width (clamped to surface capabilities).
	 * @param height Desired swapchain height (clamped to surface capabilities).
	 */
	Swapchain(const vk::raii::PhysicalDevice& physicalDevice,
	          const vk::raii::Device& device,
	          const vk::raii::SurfaceKHR& surface,
	          uint32_t width,
	          uint32_t height);
	~Swapchain();

	// Non-copyable — owns GPU resources
	Swapchain(const Swapchain&) = delete;
	Swapchain& operator=(const Swapchain&) = delete;

	// Movable
	Swapchain(Swapchain&&) noexcept = default;
	Swapchain& operator=(Swapchain&&) noexcept = default;

	/**
	 * @brief Recreates the swapchain with new dimensions.
	 *
	 * Destroys the old swapchain and creates a new one. Rebuilds image views.
	 * Call after window resize or when VK_ERROR_OUT_OF_DATE_KHR is received.
	 *
	 * @param width New desired width.
	 * @param height New desired height.
	 */
	void Recreate(uint32_t width, uint32_t height);

	/**
	 * @brief Acquires the next available swapchain image.
	 * @param signalSemaphore Semaphore to signal when the image is ready.
	 * @return Index of the acquired image.
	 * @throws std::runtime_error on fatal error (device lost).
	 */
	uint32_t AcquireNextImage(const vk::raii::Semaphore& signalSemaphore);

	/**
	 * @brief Presents the rendered image to the surface.
	 * @param waitSemaphore Semaphore to wait on before presentation.
	 * @param imageIndex Index of the image to present (from AcquireNextImage).
	 * @param presentQueue The queue to use for presentation (must be present-capable).
	 */
	void Present(const vk::raii::Semaphore& waitSemaphore, uint32_t imageIndex, vk::Queue presentQueue);

	/** @brief Current swapchain extent (width × height). */
	vk::Extent2D extent() const { return m_extent; }

	/** @brief Number of images in the swapchain. */
	uint32_t imageCount() const { return m_imageCount; }

	/** @brief The swapchain image views (indexed by acquired image index). */
	const std::vector<vk::raii::ImageView>& imageViews() const { return m_imageViews; }

	/** @brief The swapchain images (VkImage handles, indexed by acquired image index). */
	const std::vector<vk::Image>& images() const { return m_images; }

	/** @brief The swapchain image format. */
	vk::Format format() const { return m_format; }

	/** @brief Monotonically increasing generation counter. Incremented on each Recreate(). */
	uint32_t generation() const { return m_generation; }

private:
	/**
	 * @brief Selects the best available surface format.
	 * Prefers sRGB color space with BGRA8 format.
	 */
	static vk::SurfaceFormatKHR chooseSurfaceFormat(const vk::raii::PhysicalDevice& physicalDevice,
	                                                 const vk::raii::SurfaceKHR& surface);

	/**
	 * @brief Selects the best present mode.
	 * Prefers MAILBOX (low-latency) over FIFO (VSync).
	 */
	static vk::PresentModeKHR choosePresentMode(const vk::raii::PhysicalDevice& physicalDevice,
	                                             const vk::raii::SurfaceKHR& surface);

	/**
	 * @brief Clamps the desired extent to the surface's capabilities.
	 */
	static vk::Extent2D chooseExtent(const vk::raii::PhysicalDevice& physicalDevice,
	                                  const vk::raii::SurfaceKHR& surface,
	                                  uint32_t width, uint32_t height);

	/**
	 * @brief Creates image views for all swapchain images.
	 */
	std::vector<vk::raii::ImageView> createImageViews(const vk::raii::Device& device,
	                                                    const vk::raii::SwapchainKHR& swapchain,
	                                                    vk::Format format);

	const vk::raii::PhysicalDevice& m_physicalDevice;
	const vk::raii::Device& m_device;
	const vk::raii::SurfaceKHR& m_surface;

	std::unique_ptr<vk::raii::SwapchainKHR> m_swapchain;
	std::vector<vk::raii::ImageView> m_imageViews;
	std::vector<vk::Image> m_images;

	vk::Format m_format = vk::Format::eB8G8R8A8Srgb;
	vk::Extent2D m_extent = {800, 600};
	uint32_t m_imageCount = 0;
	uint32_t m_generation = 0;

	uint32_t m_recreateWidth = 800;
	uint32_t m_recreateHeight = 600;
};

} // namespace neurus
