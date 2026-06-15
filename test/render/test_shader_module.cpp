#include <gtest/gtest.h>

#include "render/ShaderModule.h"
#include "render/ShaderLib.h"
#include "render/VulkanContext.h"

#include <triangle.vert.h>

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
			m_instance = VulkanContext::CreateInstance();
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
		// ShaderLib cache must be cleared before the device is destroyed,
		// otherwise cached ShaderModules outlive their Vulkan device.
		ShaderLib::Clear();
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

	auto module = ShaderModule::FromEmbedded(*m_device, triangle_vert_spv, triangle_vert_spv_size);

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
		triangle_vert_spv,
		triangle_vert_spv + (triangle_vert_spv_size / sizeof(uint32_t)));

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
		triangle_vert_spv,
		triangle_vert_spv + (triangle_vert_spv_size / sizeof(uint32_t)));

	ShaderModule original(*m_device, spirv);
	EXPECT_NE(*original.handle(), VK_NULL_HANDLE);

	// Move-construct
	ShaderModule moved(std::move(original));
	EXPECT_NE(*moved.handle(), VK_NULL_HANDLE);

	// Based on vk::raii move semantics, the moved-from ShaderModule
	// holds VK_NULL_HANDLE. Do not dereference after move.
}

// ---------------------------------------------------------------------------
// ShaderLib tests
// ---------------------------------------------------------------------------

TEST_F(ShaderModuleTest, ShaderLib_LoadShader_CachesAndReturns)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Clear any previous cache state
	ShaderLib::Clear();

	auto first = ShaderLib::LoadShader(*m_device, "triangle_vert", triangle_vert_spv, triangle_vert_spv_size);
	ASSERT_NE(first, nullptr);
	EXPECT_NE(*first->handle(), VK_NULL_HANDLE);

	// Second load with same name should return the cached instance
	auto second = ShaderLib::LoadShader(*m_device, "triangle_vert", triangle_vert_spv, triangle_vert_spv_size);
	ASSERT_NE(second, nullptr);

	// Same shared_ptr (same pointer, not just same handle)
	EXPECT_EQ(first.get(), second.get());
}

TEST_F(ShaderModuleTest, ShaderLib_LoadDifferentShaders_AreDistinct)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ShaderLib::Clear();

	auto vert = ShaderLib::LoadShader(*m_device, "vertex", triangle_vert_spv, triangle_vert_spv_size);
	auto frag = ShaderLib::LoadShader(*m_device, "fragment", triangle_vert_spv, triangle_vert_spv_size);

	ASSERT_NE(vert, nullptr);
	ASSERT_NE(frag, nullptr);
	EXPECT_NE(vert.get(), frag.get());
}

TEST_F(ShaderModuleTest, ShaderLib_Clear_RemovesAll)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ShaderLib::Clear();

	auto shader = ShaderLib::LoadShader(*m_device, "test_shader", triangle_vert_spv, triangle_vert_spv_size);
	ASSERT_NE(shader, nullptr);

	ShaderLib::Clear();

	// Reload after clear should create a new instance
	auto reloaded = ShaderLib::LoadShader(*m_device, "test_shader", triangle_vert_spv, triangle_vert_spv_size);
	ASSERT_NE(reloaded, nullptr);

	// Should be different pointer after clear
	EXPECT_NE(shader.get(), reloaded.get());
}
