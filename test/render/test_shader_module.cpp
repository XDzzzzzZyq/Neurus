#include <gtest/gtest.h>

#include "render/shaders/ShaderModule.h"
#include "render/shaders/ShaderLibrary.h"
#include "render/shaders/ComputeShader.h"
#include "app/VulkanContext.h"
#include "platform/PlatformSurface.h"
#include "shared/TestMinimalSpv.h"

using namespace neurus;

/**
 * @brief Tests for ShaderModule and ShaderLib.
 *
 * These tests require a Vulkan instance and device.
 * In CI environments without GPU, they are skipped.
 *
 * @note Uses a headless device (no surface) since shader modules
 *       do not require presentation support.
 */
class ShaderModuleTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			auto platform = CreatePlatformSurface();
			m_instance = VulkanContext::CreateInstance(*platform);
			m_physicalDevices = vk::raii::PhysicalDevices(m_instance);
			m_hasVulkan = !m_physicalDevices.empty();

			if (m_hasVulkan)
			{
				m_device = std::make_unique<vk::raii::Device>(createHeadlessDevice());
			}
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		// ShaderLibrary cache must be cleared before the device is destroyed,
		// otherwise cached ShaderModules outlive their Vulkan device.
		ShaderLibrary::Clear();
	}

	/**
	 * @brief Creates a logical device without requiring a surface.
	 *
	 * Picks the first physical device with a graphics-capable queue family.
	 * This is sufficient for shader module creation.
	 */
	vk::raii::Device createHeadlessDevice()
	{
		auto& physDevice = m_physicalDevices[0];
		auto queueFamilyProps = physDevice.getQueueFamilyProperties();

		uint32_t graphicsFamily = ~0u;
		for (uint32_t i = 0; i < queueFamilyProps.size(); ++i)
		{
			if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
			{
				graphicsFamily = i;
				break;
			}
		}

		if (graphicsFamily == ~0u)
		{
			throw std::runtime_error("No graphics queue family found on physical device");
		}

		float priority = 1.0f;
		vk::DeviceQueueCreateInfo queueCreateInfo({}, graphicsFamily, 1, &priority);
		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreateInfo);

		return vk::raii::Device(physDevice, deviceCreateInfo);
	}

	bool m_hasVulkan = false;
	vk::raii::Instance m_instance = nullptr;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;
	std::unique_ptr<vk::raii::Device> m_device;
};

// ---------------------------------------------------------------------------
// ShaderModule tests
// ---------------------------------------------------------------------------

TEST_F(ShaderModuleTest, FromEmbedded_CreatesValidModule)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto module = ShaderModule::FromEmbedded(*m_device, kMinimalCompSpv, kMinimalCompSpvSize);

	// Verify the handle contains a valid VkShaderModule (not VK_NULL_HANDLE)
	EXPECT_NE(*module.handle(), VK_NULL_HANDLE);
}

TEST_F(ShaderModuleTest, Constructor_CreatesValidModule)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<uint32_t> spirv(
		kMinimalCompSpv,
		kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));

	ShaderModule module(*m_device, spirv);

	EXPECT_NE(*module.handle(), VK_NULL_HANDLE);
}

TEST_F(ShaderModuleTest, MoveConstructor_TransfersOwnership)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<uint32_t> spirv(
		kMinimalCompSpv,
		kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));

	ShaderModule original(*m_device, spirv);
	EXPECT_NE(*original.handle(), VK_NULL_HANDLE);

	// Move-construct
	ShaderModule moved(std::move(original));
	EXPECT_NE(*moved.handle(), VK_NULL_HANDLE);

	// Based on vk::raii move semantics, the moved-from ShaderModule
	// holds VK_NULL_HANDLE. Do not dereference after move.
}

// ---------------------------------------------------------------------------
// ShaderLibrary cache tests
// ---------------------------------------------------------------------------

TEST_F(ShaderModuleTest, FromEmbedded_MultipleCallsAllValid)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create two modules from the same SPIR-V data
	auto moduleA = ShaderModule::FromEmbedded(
		*m_device, kMinimalCompSpv, kMinimalCompSpvSize);
	auto moduleB = ShaderModule::FromEmbedded(
		*m_device, kMinimalCompSpv, kMinimalCompSpvSize);

	EXPECT_NE(*moduleA.handle(), VK_NULL_HANDLE);
	EXPECT_NE(*moduleB.handle(), VK_NULL_HANDLE);

	// FromEmbedded returns distinct objects (no caching)
	EXPECT_NE(*moduleA.handle(), *moduleB.handle());
}

TEST_F(ShaderModuleTest, FromSpirV_DifferentData_DistinctModules)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create modules from different SPIR-V data sets
	auto vertSpirv = std::vector<uint32_t>(
		kMinimalVertSpv,
		kMinimalVertSpv + (kMinimalVertSpvSize / sizeof(uint32_t)));
	auto fragSpirv = std::vector<uint32_t>(
		kMinimalFragSpv,
		kMinimalFragSpv + (kMinimalFragSpvSize / sizeof(uint32_t)));

	auto vertModule = ShaderModule::FromSpirV(*m_device, vertSpirv);
	auto fragModule = ShaderModule::FromSpirV(*m_device, fragSpirv);

	ASSERT_NE(vertModule, nullptr);
	ASSERT_NE(fragModule, nullptr);
	EXPECT_NE(*vertModule->handle(), *fragModule->handle());
}

TEST_F(ShaderModuleTest, FromEmbedded_ReuseAfterDestruction)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create a module and let it be destroyed (scope exit)
	VkShaderModule firstHandle = VK_NULL_HANDLE;
	{
		auto module = ShaderModule::FromEmbedded(
			*m_device, kMinimalCompSpv, kMinimalCompSpvSize);
		ASSERT_NE(*module.handle(), VK_NULL_HANDLE);
		firstHandle = *module.handle();
	}

	// Create a new module from the same data — must succeed
	auto module = ShaderModule::FromEmbedded(
		*m_device, kMinimalCompSpv, kMinimalCompSpvSize);
	EXPECT_NE(*module.handle(), VK_NULL_HANDLE);

	// Note: Some drivers (e.g., MoltenVK) may reuse the same handle value
	// after destruction. We only verify the new module is valid, not that
	// the handle differs from the destroyed one.
}
