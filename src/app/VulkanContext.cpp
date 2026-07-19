// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include "VulkanContext.h"

#include "core/Log.h"

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

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void*)
{
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		std::cerr << "[Vulkan Validation] " << pCallbackData->pMessage << "\n";
	return VK_FALSE;
}

static std::vector<const char*> getRequiredInstanceExtensions()
{
	std::vector<const char*> extensions;
	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
	if constexpr (kEnableValidation)
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	return extensions;
}

vk::raii::Instance VulkanContext::CreateInstance()
{
	vk::ApplicationInfo appInfo("Neurus", VK_MAKE_VERSION(0,1,0), "NeurusRenderer", VK_MAKE_VERSION(0,1,0), VK_API_VERSION_1_4);
	auto extensions = getRequiredInstanceExtensions();

	vk::InstanceCreateInfo createInfo({}, &appInfo, {}, extensions);

	if constexpr (kEnableValidation)
	{
		auto layers = vk::enumerateInstanceLayerProperties();
		bool ok = true;
		for (auto* name : kValidationLayers) {
			bool found = false;
			for (auto& l : layers) { if (!strcmp(name, l.layerName)) { found = true; break; } }
			if (!found) { ok = false; break; }
		}
		if (ok) {
			createInfo.setPEnabledLayerNames(kValidationLayers);
			vk::DebugUtilsMessengerCreateInfoEXT dbg({},
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
				debugCallback);
			createInfo.setPNext(&dbg);
		} else {
			std::cerr << "[Neurus] Validation layers not available.\n";
		}
	}

	vk::raii::Context context;
	return vk::raii::Instance(context, createInfo);
}

VulkanContext::VulkanContext(vk::raii::Instance&& instance)
{
	ctx_instance = std::make_unique<vk::raii::Instance>(std::move(instance));
}

void VulkanContext::InitDevice()
{
	ctx_physicalDevices = vk::raii::PhysicalDevices(*ctx_instance);
	ctx_selectedDeviceIndex = selectPhysicalDeviceIndex();
	auto& pd = ctx_physicalDevices[ctx_selectedDeviceIndex];
	auto props = pd.getProperties();
	ctx_gpuName = props.deviceName.data();

	NEURUS_LOG("[VulkanContext] " << ctx_gpuName
	           << " | limits.maxPushConstantsSize = "
	           << props.limits.maxPushConstantsSize);

	// Find queue families
	ctx_graphicsQueueFamily = findGraphicsQueueFamily();
	ctx_transferQueueFamily = findTransferQueueFamily();

	float graphicsPriority = 1.0f;
	std::vector<vk::DeviceQueueCreateInfo> queueCIs;
	queueCIs.push_back(vk::DeviceQueueCreateInfo({}, ctx_graphicsQueueFamily, 1, &graphicsPriority));

	if (ctx_transferQueueFamily != ctx_graphicsQueueFamily)
	{
		float transferPriority = 0.5f;
		queueCIs.push_back(vk::DeviceQueueCreateInfo({}, ctx_transferQueueFamily, 1, &transferPriority));
	}

	vk::PhysicalDeviceMultiviewFeatures multiviewFeature;
	multiviewFeature.multiview = VK_TRUE;
	vk::PhysicalDeviceDynamicRenderingFeatures dynRendering;
	dynRendering.dynamicRendering = VK_TRUE;
	dynRendering.pNext = &multiviewFeature;
	vk::PhysicalDeviceSynchronization2Features sync2;
	sync2.synchronization2 = VK_TRUE;
	sync2.pNext = &dynRendering;

	vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexing;
	descriptorIndexing.descriptorBindingPartiallyBound = VK_TRUE;
	descriptorIndexing.pNext = &sync2;

	vk::PhysicalDeviceFeatures features;
	std::vector<const char*> devExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	// VK_KHR_portability_subset is required when a profile layer simulates portability
	// (e.g. VP_LUNARG_desktop_baseline). Check if supported and add to extensions.
	auto availableDevExtensions = pd.enumerateDeviceExtensionProperties();
	for (const auto& ext : availableDevExtensions)
	{
		if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0)
		{
			devExts.push_back("VK_KHR_portability_subset");
			break;
		}
	}

	vk::DeviceCreateInfo devCI({}, queueCIs, {}, devExts, &features, &descriptorIndexing);

	ctx_device = std::make_unique<vk::raii::Device>(pd, devCI);

	// Get transfer queue handle immediately (does not need present check)
	ctx_transferQueue = ctx_device->getQueue(ctx_transferQueueFamily, 0);
}

void VulkanContext::InitQueue(const vk::raii::SurfaceKHR& surface)
{
	// Re-evaluate queue family that supports presentation to this surface
	ctx_graphicsQueueFamily = findGraphicsQueueFamilyWithPresent(surface);
	ctx_graphicsQueue = ctx_device->getQueue(ctx_graphicsQueueFamily, 0);
}

VulkanContext::~VulkanContext() {}

uint32_t VulkanContext::selectPhysicalDeviceIndex()
{
	if (ctx_physicalDevices.empty())
		throw std::runtime_error("No Vulkan-capable GPUs found.");
	for (uint32_t i = 0; i < (uint32_t)ctx_physicalDevices.size(); ++i)
		if (ctx_physicalDevices[i].getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
			return i;
	return 0;
}

uint32_t VulkanContext::findGraphicsQueueFamily()
{
	auto& pd = ctx_physicalDevices[ctx_selectedDeviceIndex];
	auto qf = pd.getQueueFamilyProperties();
	for (uint32_t i = 0; i < (uint32_t)qf.size(); ++i)
	{
		if (qf[i].queueFlags & vk::QueueFlagBits::eGraphics)
			return i;
	}
	throw std::runtime_error("No graphics-capable queue family found.");
}

uint32_t VulkanContext::findGraphicsQueueFamilyWithPresent(const vk::raii::SurfaceKHR& surface)
{
	auto& pd = ctx_physicalDevices[ctx_selectedDeviceIndex];
	auto qf = pd.getQueueFamilyProperties();
	for (uint32_t i = 0; i < (uint32_t)qf.size(); ++i)
	{
		if (!(qf[i].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
		if (!pd.getSurfaceSupportKHR(i, *surface)) continue;
		return i;
	}
	throw std::runtime_error("No queue family with graphics + present.");
}

// ---------------------------------------------------------------------------
// findTransferQueueFamily
// ---------------------------------------------------------------------------

uint32_t VulkanContext::findTransferQueueFamily()
{
	auto& pd = ctx_physicalDevices[ctx_selectedDeviceIndex];
	auto qf = pd.getQueueFamilyProperties();

	// Prefer a dedicated transfer-only queue (has eTransfer but not eGraphics).
	for (uint32_t i = 0; i < (uint32_t)qf.size(); ++i)
	{
		if (qf[i].queueFlags & vk::QueueFlagBits::eTransfer &&
		    !(qf[i].queueFlags & vk::QueueFlagBits::eGraphics))
		{
			return i;
		}
	}

	// Fall back to the graphics queue family (graphics queues always support transfer).
	return ctx_graphicsQueueFamily;
}

} // namespace neurus
