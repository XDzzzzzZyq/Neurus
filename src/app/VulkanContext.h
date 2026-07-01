#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <string>
#include <vector>
#include <memory>

namespace neurus {

/**
 * @brief Manages the Vulkan instance, physical device, and logical device.
 *
 * Two-phase construction to avoid moving the instance while the surface
 * holds a reference to its dispatcher:
 *   1. VulkanContext(std::move(instance)) - stores the instance (no move during surface lifetime)
 *   2. initDevice(surface) - creates physical device + logical device
 */
class VulkanContext
{
public:
	/** @brief Creates a Vulkan instance with required extensions. */
	static vk::raii::Instance CreateInstance();

	/** @brief Takes ownership of the instance (from CreateInstance). */
	explicit VulkanContext(vk::raii::Instance&& instance);

	/** @brief Creates physical device + logical device using the given surface. */
	void initDevice(const vk::raii::SurfaceKHR& surface);

	~VulkanContext();

	VulkanContext(const VulkanContext&) = delete;
	VulkanContext& operator=(const VulkanContext&) = delete;
	VulkanContext(VulkanContext&&) noexcept = default;
	VulkanContext& operator=(VulkanContext&&) noexcept = default;

	const vk::raii::Instance& instance() const { return *ctx_instance; }
	const vk::raii::Device& device() const { return *ctx_device; }
	const vk::raii::PhysicalDevice& physicalDevice() const { return ctx_physicalDevices[ctx_selectedDeviceIndex]; }
	uint32_t graphicsQueueFamily() const { return ctx_graphicsQueueFamily; }
	vk::Queue graphicsQueue() const { return ctx_graphicsQueue; }
	const std::string& gpuName() const { return ctx_gpuName; }

private:
	static vk::raii::Instance createInstanceInternal();
	uint32_t selectPhysicalDeviceIndex();
	uint32_t findGraphicsQueueFamily(const vk::raii::SurfaceKHR& surface);

	std::unique_ptr<vk::raii::Instance> ctx_instance;
	vk::raii::PhysicalDevices ctx_physicalDevices = nullptr;
	uint32_t ctx_selectedDeviceIndex = 0;
	std::unique_ptr<vk::raii::Device> ctx_device;

	uint32_t ctx_graphicsQueueFamily = 0;
	vk::Queue ctx_graphicsQueue = nullptr;
	std::string ctx_gpuName;
};

} // namespace neurus
