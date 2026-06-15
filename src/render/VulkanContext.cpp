// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR
// Suppress VK_HEADER_VERSION assertion (GPU driver DLL may differ from SDK headers)
#define VULKAN_HPP_ASSERT(x) ((void)0)

#include "VulkanContext.h"

#include <stdexcept>
#include <set>
#include <cstring>
#include <iostream>

namespace neurus {

#ifdef _DEBUG
	constexpr bool kEnableValidation = true;
#else
	constexpr bool kEnableValidation = false;
#endif

const std::vector<const char*> kValidationLayers = {
	"VK_LAYER_KHRONOS_validation",
};

// --- Debug messenger callback for validation layers ---
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* /*pUserData*/)
{
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cerr << "[Vulkan Validation] " << pCallbackData->pMessage << "\n";
	}
	return VK_FALSE;
}

// --- Required instance extensions ---
static std::vector<const char*> getRequiredInstanceExtensions()
{
	std::vector<const char*> extensions;

	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

	if constexpr (kEnableValidation)
	{
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return extensions;
}

// --- Instance creation ---
vk::raii::Instance VulkanContext::CreateInstance()
{
	vk::ApplicationInfo appInfo(
		"Neurus", VK_MAKE_VERSION(0, 1, 0),
		"NeurusRenderer", VK_MAKE_VERSION(0, 1, 0),
		VK_API_VERSION_1_4
	);

	auto extensions = getRequiredInstanceExtensions();

	vk::InstanceCreateInfo createInfo(
		{},
		&appInfo,
		{},
		extensions
	);

	if constexpr (kEnableValidation)
	{
		// Check if validation layers are available
		auto availableLayers = vk::enumerateInstanceLayerProperties();
		bool layersAvailable = true;
		for (const char* layerName : kValidationLayers)
		{
			bool found = false;
			for (const auto& layer : availableLayers)
			{
				if (strcmp(layerName, layer.layerName) == 0)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				layersAvailable = false;
				break;
			}
		}

		if (layersAvailable)
		{
			createInfo.setPEnabledLayerNames(kValidationLayers);

			vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo(
				{},
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
				vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
				vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
				debugCallback
			);

			createInfo.setPNext(&debugCreateInfo);
		}
		else
		{
			std::cerr << "[Neurus] Validation layers requested but not available.\n";
		}
	}

	vk::raii::Context context;
	return vk::raii::Instance(context, createInfo);
}

// --- Constructor ---
VulkanContext::VulkanContext(vk::raii::Instance&& instance, const vk::raii::SurfaceKHR& surface)
{
	// Take ownership of the instance
	m_instance = std::make_unique<vk::raii::Instance>(std::move(instance));

	// Enumerate physical devices (owned by this member for lifetime)
	m_physicalDevices = vk::raii::PhysicalDevices(*m_instance);

	// Pick the best device
	m_selectedDeviceIndex = selectPhysicalDeviceIndex(*m_instance);

	// Get device properties
	auto properties = m_physicalDevices[m_selectedDeviceIndex].getProperties();
	m_gpuName = properties.deviceName.data();

	// Find graphics + present queue family
	m_graphicsQueueFamily = findGraphicsQueueFamily(surface);

	// Create logical device
	float queuePriority = 1.0f;
	vk::DeviceQueueCreateInfo queueCreateInfo(
		{},
		m_graphicsQueueFamily,
		1,
		&queuePriority
	);

	// Enable dynamic rendering (Vulkan 1.3+ feature)
	vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature;
	dynamicRenderingFeature.dynamicRendering = VK_TRUE;

	// Enable synchronization2 (useful for modern barrier patterns)
	vk::PhysicalDeviceSynchronization2Features sync2Feature;
	sync2Feature.synchronization2 = VK_TRUE;
	sync2Feature.pNext = &dynamicRenderingFeature;

	// Device features (empty for triangle — no features needed)
	vk::PhysicalDeviceFeatures deviceFeatures;

	// Request swapchain extension
	std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	vk::DeviceCreateInfo deviceCreateInfo(
		{},
		queueCreateInfo,
		{},  // No validation layers for device (instance-level only)
		deviceExtensions,
		&deviceFeatures,
		&sync2Feature
	);

	m_device = std::make_unique<vk::raii::Device>(m_physicalDevices[m_selectedDeviceIndex], deviceCreateInfo);

	// Get queue handle
	m_graphicsQueue = m_device->getQueue(m_graphicsQueueFamily, 0);
}

VulkanContext::~VulkanContext()
{
	// vk::raii handles cleanup automatically
}

uint32_t VulkanContext::selectPhysicalDeviceIndex(vk::raii::Instance& instance)
{
	if (m_physicalDevices.empty())
	{
		throw std::runtime_error("No Vulkan-capable GPUs found.");
	}

	// Prefer discrete GPU
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_physicalDevices.size()); ++i)
	{
		auto properties = m_physicalDevices[i].getProperties();
		if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
		{
			return i;
		}
	}

	// Fallback to first available
	return 0;
}

uint32_t VulkanContext::findGraphicsQueueFamily(const vk::raii::SurfaceKHR& surface)
{
	const auto& physicalDevice = m_physicalDevices[m_selectedDeviceIndex];
	auto queueFamilies = physicalDevice.getQueueFamilyProperties();

	for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilies.size()); ++i)
	{
		if (!(queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics))
		{
			continue;
		}

		if (!physicalDevice.getSurfaceSupportKHR(i, *surface))
		{
			continue;
		}

		return i;
	}

	throw std::runtime_error("No queue family supports both graphics and presentation.");
}

} // namespace neurus
