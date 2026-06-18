/**
 * @file TestVulkanFixture.cpp
 * @brief Implementation of the shared GPU test fixture base class.
 */

#include "TestVulkanFixture.h"

#include <iostream>

// ===========================================================================
// SetUp — standard Vulkan bootstrap
// ===========================================================================

void VulkanTestFixture::SetUp()
{
	try
	{
		// --- Instance ---
		vk::ApplicationInfo appInfo("NeurusTest",
		                            VK_MAKE_VERSION(0, 5, 0),
		                            "NeurusTest",
		                            VK_MAKE_VERSION(0, 5, 0),
		                            VK_API_VERSION_1_4);
		std::vector<const char*> instanceExts = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
		};
		vk::InstanceCreateInfo instanceCI({}, &appInfo, {}, instanceExts);
		m_instance = std::make_unique<vk::raii::Instance>(m_context, instanceCI);

		// --- Physical device ---
		m_physicalDevices = vk::raii::PhysicalDevices(*m_instance);
		if (m_physicalDevices.empty())
		{
			m_hasVulkan = false;
			return;
		}

		// Pick discrete GPU if available
		m_selectedPdIndex = 0;
		for (uint32_t i = 0; i < static_cast<uint32_t>(m_physicalDevices.size()); ++i)
		{
			const auto props = m_physicalDevices[i].getProperties();
			if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
			{
				m_selectedPdIndex = i;
				break;
			}
		}
		auto& pd = m_physicalDevices[m_selectedPdIndex];

		// --- Queue family ---
		auto qfProps = pd.getQueueFamilyProperties();
		m_graphicsQueueFamily = UINT32_MAX;
		for (uint32_t i = 0; i < static_cast<uint32_t>(qfProps.size()); ++i)
		{
			if (qfProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
			{
				m_graphicsQueueFamily = i;
				break;
			}
		}
		if (m_graphicsQueueFamily == UINT32_MAX)
		{
			m_hasVulkan = false;
			return;
		}

		// --- Device ---
		float prio = 1.0f;
		vk::DeviceQueueCreateInfo qCI({}, m_graphicsQueueFamily, 1, &prio);
		vk::PhysicalDeviceFeatures features;
		vk::DeviceCreateInfo devCI({}, qCI, {}, {}, &features);
		m_device = std::make_unique<vk::raii::Device>(pd, devCI);
		m_queue = m_device->getQueue(m_graphicsQueueFamily, 0);

		// --- Command pool ---
		vk::CommandPoolCreateInfo poolCI(
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			m_graphicsQueueFamily);
		m_commandPool = std::make_unique<vk::raii::CommandPool>(*m_device, poolCI);

		// --- Command buffers ---
		vk::CommandBufferAllocateInfo allocInfo(
			*m_commandPool, vk::CommandBufferLevel::ePrimary, 1);
		m_commandBuffers = vk::raii::CommandBuffers(*m_device, allocInfo);

		m_hasVulkan = true;
	}
	catch (const std::exception& e)
	{
		std::cerr << "[VulkanTestFixture::SetUp] " << e.what() << std::endl;
		m_hasVulkan = false;
	}
	catch (...)
	{
		m_hasVulkan = false;
	}
}

// ===========================================================================
// TearDown
// ===========================================================================

void VulkanTestFixture::TearDown()
{
	if (m_device)
	{
		m_device->waitIdle();
	}
}

// ===========================================================================
// Command buffer helpers
// ===========================================================================

vk::raii::CommandBuffer& VulkanTestFixture::BeginCmd()
{
	auto& cmd = m_commandBuffers[0];
	cmd.begin(
		vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	return cmd;
}

void VulkanTestFixture::EndSubmitWait(vk::raii::CommandBuffer& cmd)
{
	cmd.end();
	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmd));
	m_queue.submit(submitInfo, nullptr);
	m_device->waitIdle();
}

// ===========================================================================
// Asset path resolution
// ===========================================================================

std::string VulkanTestFixture::ResolveAssetPath(const char* assetRelative)
{
	// Try: relative from build/debug/Debug/ (MSVC multi-config layout)
	std::string path1 = std::string("../../../") + assetRelative;
	{
		std::ifstream f(path1);
		if (f.good()) return path1;
	}
	// Try: relative from build/debug/ (single-config layout)
	std::string path2 = std::string("../../") + assetRelative;
	{
		std::ifstream f(path2);
		if (f.good()) return path2;
	}
	// Fallback: return the primary path and let the caller handle failure
	return path1;
}
