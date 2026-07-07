#include <gtest/gtest.h>

#include "render/shaders/ShaderModule.h"
#include "render/shaders/ShaderLibrary.h"
#include "render/shaders/ComputeShader.h"
#include "app/VulkanContext.h"

using namespace neurus;

// Valid minimal SPIR-V arrays generated via glslangValidator
static const uint32_t kMinimalVertSpv[] = {
	0x07230203, 0x00010000, 0x0008000B, 0x00000014,
	0x00000000, 0x00020011, 0x00000001, 0x0006000B,
	0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
	0x00000000, 0x0003000E, 0x00000000, 0x00000001,
	0x0006000F, 0x00000000, 0x00000004, 0x6E69616D,
	0x00000000, 0x0000000D, 0x00030003, 0x00000002,
	0x000001C2, 0x00040005, 0x00000004, 0x6E69616D,
	0x00000000, 0x00060005, 0x0000000B, 0x505F6C67,
	0x65567265, 0x78657472, 0x00000000, 0x00060006,
	0x0000000B, 0x00000000, 0x505F6C67, 0x7469736F,
	0x006E6F69, 0x00070006, 0x0000000B, 0x00000001,
	0x505F6C67, 0x746E696F, 0x657A6953, 0x00000000,
	0x00070006, 0x0000000B, 0x00000002, 0x435F6C67,
	0x4470696C, 0x61747369, 0x0065636E, 0x00070006,
	0x0000000B, 0x00000003, 0x435F6C67, 0x446C6C75,
	0x61747369, 0x0065636E, 0x00030005, 0x0000000D,
	0x00000000, 0x00030047, 0x0000000B, 0x00000002,
	0x00050048, 0x0000000B, 0x00000000, 0x0000000B,
	0x00000000, 0x00050048, 0x0000000B, 0x00000001,
	0x0000000B, 0x00000001, 0x00050048, 0x0000000B,
	0x00000002, 0x0000000B, 0x00000003, 0x00050048,
	0x0000000B, 0x00000003, 0x0000000B, 0x00000004,
	0x00020013, 0x00000002, 0x00030021, 0x00000003,
	0x00000002, 0x00030016, 0x00000006, 0x00000020,
	0x00040017, 0x00000007, 0x00000006, 0x00000004,
	0x00040015, 0x00000008, 0x00000020, 0x00000000,
	0x0004002B, 0x00000008, 0x00000009, 0x00000001,
	0x0004001C, 0x0000000A, 0x00000006, 0x00000009,
	0x0006001E, 0x0000000B, 0x00000007, 0x00000006,
	0x0000000A, 0x0000000A, 0x00040020, 0x0000000C,
	0x00000003, 0x0000000B, 0x0004003B, 0x0000000C,
	0x0000000D, 0x00000003, 0x00040015, 0x0000000E,
	0x00000020, 0x00000001, 0x0004002B, 0x0000000E,
	0x0000000F, 0x00000000, 0x0004002B, 0x00000006,
	0x00000010, 0x00000000, 0x0007002C, 0x00000007,
	0x00000011, 0x00000010, 0x00000010, 0x00000010,
	0x00000010, 0x00040020, 0x00000012, 0x00000003,
	0x00000007, 0x00050036, 0x00000002, 0x00000004,
	0x00000000, 0x00000003, 0x000200F8, 0x00000005,
	0x00050041, 0x00000012, 0x00000013, 0x0000000D,
	0x0000000F, 0x0003003E, 0x00000013, 0x00000011,
	0x000100FD, 0x00010038,
};
static const size_t kMinimalVertSpvSize = sizeof(kMinimalVertSpv);

static const uint32_t kMinimalFragSpv[] = {
	0x07230203, 0x00010000, 0x0008000B, 0x0000000C,
	0x00000000, 0x00020011, 0x00000001, 0x0006000B,
	0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
	0x00000000, 0x0003000E, 0x00000000, 0x00000001,
	0x0006000F, 0x00000004, 0x00000004, 0x6E69616D,
	0x00000000, 0x00000009, 0x00030010, 0x00000004,
	0x00000007, 0x00030003, 0x00000002, 0x000001C2,
	0x00040005, 0x00000004, 0x6E69616D, 0x00000000,
	0x00050005, 0x00000009, 0x4374756F, 0x726F6C6F,
	0x00000000, 0x00040047, 0x00000009, 0x0000001E,
	0x00000000, 0x00020013, 0x00000002, 0x00030021,
	0x00000003, 0x00000002, 0x00030016, 0x00000006,
	0x00000020, 0x00040017, 0x00000007, 0x00000006,
	0x00000004, 0x00040020, 0x00000008, 0x00000003,
	0x00000007, 0x0004003B, 0x00000008, 0x00000009,
	0x00000003, 0x0004002B, 0x00000006, 0x0000000A,
	0x3F800000, 0x0007002C, 0x00000007, 0x0000000B,
	0x0000000A, 0x0000000A, 0x0000000A, 0x0000000A,
	0x00050036, 0x00000002, 0x00000004, 0x00000000,
	0x00000003, 0x000200F8, 0x00000005, 0x0003003E,
	0x00000009, 0x0000000B, 0x000100FD, 0x00010038,
};
static const size_t kMinimalFragSpvSize = sizeof(kMinimalFragSpv);

static const uint32_t kMinimalCompSpv[] = {
	0x07230203, 0x00010000, 0x0008000B, 0x0000000A,
	0x00000000, 0x00020011, 0x00000001, 0x0006000B,
	0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
	0x00000000, 0x0003000E, 0x00000000, 0x00000001,
	0x0005000F, 0x00000005, 0x00000004, 0x6E69616D,
	0x00000000, 0x00060010, 0x00000004, 0x00000011,
	0x00000001, 0x00000001, 0x00000001, 0x00030003,
	0x00000002, 0x000001C2, 0x00040005, 0x00000004,
	0x6E69616D, 0x00000000, 0x00040047, 0x00000009,
	0x0000000B, 0x00000019, 0x00020013, 0x00000002,
	0x00030021, 0x00000003, 0x00000002, 0x00040015,
	0x00000006, 0x00000020, 0x00000000, 0x00040017,
	0x00000007, 0x00000006, 0x00000003, 0x0004002B,
	0x00000006, 0x00000008, 0x00000001, 0x0006002C,
	0x00000007, 0x00000009, 0x00000008, 0x00000008,
	0x00000008, 0x00050036, 0x00000002, 0x00000004,
	0x00000000, 0x00000003, 0x000200F8, 0x00000005,
	0x000100FD, 0x00010038,
};
static const size_t kMinimalCompSpvSize = sizeof(kMinimalCompSpv);

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

	// New module gets a fresh handle (old one was destroyed)
	EXPECT_NE(*module.handle(), firstHandle);
}
