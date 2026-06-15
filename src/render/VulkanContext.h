#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <string>
#include <vector>
#include <memory>

namespace neurus {

/**
 * @brief Manages the Vulkan instance, physical device, and logical device.
 *
 * Creates its own VkInstance with required extensions. Selects a suitable
 * physical device and creates a logical device with a graphics queue.
 *
 * Uses Vulkan-HPP vk::raii namespace for automatic resource cleanup.
 *
 * Two-phase construction:
 *   1. VulkanContext::CreateInstance() — creates the VkInstance
 *   2. MainWindow creates VkSurfaceKHR from that instance
 *   3. VulkanContext(instance, surface) — creates device + queues
 *
 * This split is necessary because VkSurfaceKHR needs a VkInstance, and
 * VkDevice creation needs a VkSurfaceKHR for queue family selection.
 */
class VulkanContext
{
public:
	/**
	 * @brief Creates only the Vulkan instance. Call first.
	 * @return A fully initialized VkInstance with required extensions.
	 */
	static vk::raii::Instance CreateInstance();

	/**
	 * @brief Creates the logical device and selects physical device + queues.
	 * @param instance Previously created VkInstance (moved-in, ownership transferred).
	 * @param surface Surface for device queue selection (must support presentation).
	 */
	VulkanContext(vk::raii::Instance&& instance, const vk::raii::SurfaceKHR& surface);
	~VulkanContext();

	// Non-copyable — owns GPU resources
	VulkanContext(const VulkanContext&) = delete;
	VulkanContext& operator=(const VulkanContext&) = delete;

	// Movable
	VulkanContext(VulkanContext&&) noexcept = default;
	VulkanContext& operator=(VulkanContext&&) noexcept = default;

	/** @brief The Vulkan instance (pass to MainWindow for surface creation). */
	const vk::raii::Instance& instance() const { return *m_instance; }

	/** @brief The logical device handle (includes queue). */
	const vk::raii::Device& device() const { return *m_device; }

	/** @brief The selected physical device. */
	const vk::raii::PhysicalDevice& physicalDevice() const { return m_physicalDevices[m_selectedDeviceIndex]; }

	/** @brief Index of the graphics queue family. */
	uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamily; }

	/** @brief The graphics queue handle. */
	vk::Queue graphicsQueue() const { return m_graphicsQueue; }

	/** @brief Human-readable GPU name (e.g., "NVIDIA GeForce RTX 4090"). */
	const std::string& gpuName() const { return m_gpuName; }

private:
	/** @brief Creates the Vulkan instance with required extensions (internal helper). */
	static vk::raii::Instance createInstanceInternal();

	/** @brief Selects the best available physical device index (discrete GPU preferred). */
	uint32_t selectPhysicalDeviceIndex(vk::raii::Instance& instance);

	/** @brief Finds a queue family that supports both graphics and presentation. */
	uint32_t findGraphicsQueueFamily(const vk::raii::SurfaceKHR& surface);

	std::unique_ptr<vk::raii::Instance> m_instance;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;  // Owns all physical device handles
	uint32_t m_selectedDeviceIndex = 0;
	std::unique_ptr<vk::raii::Device> m_device;

	uint32_t m_graphicsQueueFamily = 0;
	vk::Queue m_graphicsQueue = nullptr;
	std::string m_gpuName;
};

} // namespace neurus
