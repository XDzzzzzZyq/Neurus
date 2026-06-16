// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "render/DescriptorManager.h"
#include "render/VulkanBuffer.h"
#include "render/VulkanContext.h"

using namespace neurus;

// ---------------------------------------------------------------------------
// Non-GPU tests — Fluent builder (no Vulkan device needed)
// ---------------------------------------------------------------------------

/**
 * @brief Tests for the DescriptorSetLayoutBuilder fluent API.
 *
 * These tests verify binding accumulation without requiring a Vulkan device.
 */
TEST(DescriptorSetLayoutBuilderTest, AddSingleBinding_BuildsCorrectly)
{
	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	ASSERT_EQ(bindings.size(), 1u);
	EXPECT_EQ(bindings[0].binding, 0u);
	EXPECT_EQ(bindings[0].descriptorType, vk::DescriptorType::eUniformBuffer);
	EXPECT_EQ(bindings[0].descriptorCount, 1u);
	EXPECT_EQ(bindings[0].stageFlags, vk::ShaderStageFlagBits::eVertex);
}

TEST(DescriptorSetLayoutBuilderTest, AddMultipleBindings_BuildsAll)
{
	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .AddBinding(1, vk::DescriptorType::eCombinedImageSampler,
	                                vk::ShaderStageFlagBits::eFragment)
	                    .AddBinding(2, vk::DescriptorType::eStorageBuffer,
	                                vk::ShaderStageFlagBits::eCompute)
	                    .Build();

	ASSERT_EQ(bindings.size(), 3u);

	EXPECT_EQ(bindings[0].binding, 0u);
	EXPECT_EQ(bindings[0].descriptorType, vk::DescriptorType::eUniformBuffer);

	EXPECT_EQ(bindings[1].binding, 1u);
	EXPECT_EQ(bindings[1].descriptorType, vk::DescriptorType::eCombinedImageSampler);

	EXPECT_EQ(bindings[2].binding, 2u);
	EXPECT_EQ(bindings[2].descriptorType, vk::DescriptorType::eStorageBuffer);
}

TEST(DescriptorSetLayoutBuilderTest, AddBindingWithCount_StoresDescriptorCount)
{
	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
	                                vk::ShaderStageFlagBits::eFragment, 4)
	                    .Build();

	ASSERT_EQ(bindings.size(), 1u);
	EXPECT_EQ(bindings[0].descriptorCount, 4u);
}

TEST(DescriptorSetLayoutBuilderTest, EmptyBuilder_ReturnsEmptyVector)
{
	auto bindings = BuildLayout().Build();
	EXPECT_TRUE(bindings.empty());
}

TEST(DescriptorSetLayoutBuilderTest, ChainedAddBinding_ReturnsBuilderReference)
{
	auto& ref = BuildLayout().AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                     vk::ShaderStageFlagBits::eVertex);
	// Compile-time check: AddBinding returns a reference to the builder
	// for fluent chaining.
	EXPECT_TRUE(true);
	(void)ref;
}

// ---------------------------------------------------------------------------
// GPU tests — Layout, Pool, Set allocation and writing
// ---------------------------------------------------------------------------

/**
 * @brief GPU test fixture for descriptor management tests.
 *
 * Creates an instance and device. Tests are skipped if no Vulkan-capable
 * GPU is available.
 */
class DescriptorManagerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Create instance ---
			m_instance = std::make_unique<vk::raii::Instance>(
				VulkanContext::CreateInstance());

			// --- Enumerate physical devices ---
			m_physicalDevices =
				std::make_unique<vk::raii::PhysicalDevices>(*m_instance);
			if (m_physicalDevices->empty())
			{
				m_hasVulkan = false;
				return;
			}

			// --- Find a queue family with graphics bit ---
			auto qfProps =
				(*m_physicalDevices)[0].getQueueFamilyProperties();
			m_queueFamilyIndex = UINT32_MAX;
			for (uint32_t i = 0;
			     i < static_cast<uint32_t>(qfProps.size()); ++i)
			{
				if (qfProps[i].queueFlags &
				    vk::QueueFlagBits::eGraphics)
				{
					m_queueFamilyIndex = i;
					break;
				}
			}

			if (m_queueFamilyIndex == UINT32_MAX)
			{
				m_hasVulkan = false;
				return;
			}

			// --- Create logical device ---
			float prio = 1.0f;
			vk::DeviceQueueCreateInfo qCI(
				{}, m_queueFamilyIndex, 1, &prio);
			vk::DeviceCreateInfo devCI({}, qCI);

			m_device = std::make_unique<vk::raii::Device>(
				(*m_physicalDevices)[0], devCI);

			m_queue =
				m_device->getQueue(m_queueFamilyIndex, 0);
			m_hasVulkan = true;
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		if (m_device)
		{
			m_device->waitIdle();
		}
		// Destroy in reverse order (RAII)
		m_device.reset();
		m_physicalDevices.reset();
		m_instance.reset();
	}

	std::unique_ptr<vk::raii::Instance> m_instance;
	std::unique_ptr<vk::raii::PhysicalDevices> m_physicalDevices;
	std::unique_ptr<vk::raii::Device> m_device;
	vk::Queue m_queue = nullptr;
	uint32_t m_queueFamilyIndex = 0;
	bool m_hasVulkan = false;
};

// ---------------------------------------------------------------------------
// DescriptorSetLayout creation
// ---------------------------------------------------------------------------

TEST_F(DescriptorManagerTest, CreateDescriptorSetLayout_WithSingleBinding)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	ASSERT_NO_THROW({
		DescriptorSetLayout layout(*m_device, bindings);
		EXPECT_EQ(layout.bindings().size(), 1u);
	});
}

TEST_F(DescriptorManagerTest, CreateDescriptorSetLayout_WithMultipleBindings)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .AddBinding(1, vk::DescriptorType::eCombinedImageSampler,
	                                vk::ShaderStageFlagBits::eFragment)
	                    .Build();

	ASSERT_NO_THROW({
		DescriptorSetLayout layout(*m_device, bindings);
		EXPECT_EQ(layout.bindings().size(), 2u);
	});
}

TEST_F(DescriptorManagerTest, DescriptorSetLayout_ReturnsValidVkLayout)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	DescriptorSetLayout layout(*m_device, bindings);

	// The underlying vk::raii::DescriptorSetLayout must be non-null
	const auto& vkLayout = layout.layout();
	EXPECT_NE(*vkLayout, vk::DescriptorSetLayout{});
}

// ---------------------------------------------------------------------------
// DescriptorPool creation
// ---------------------------------------------------------------------------

TEST_F(DescriptorManagerTest, CreateDescriptorPool_WithPoolSizes)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{vk::DescriptorType::eUniformBuffer, 8},
		{vk::DescriptorType::eCombinedImageSampler, 4},
	};

	ASSERT_NO_THROW({
		DescriptorPool pool(*m_device, 16, poolSizes);
	});
}

TEST_F(DescriptorManagerTest, CreateDescriptorPool_WithZeroMaxSets)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{vk::DescriptorType::eUniformBuffer, 1},
	};

	// Pool with maxSets=0 is valid per spec (can allocate 0 sets)
	ASSERT_NO_THROW({
		DescriptorPool pool(*m_device, 0, poolSizes);
	});
}

// ---------------------------------------------------------------------------
// CalculatePoolSizes
// ---------------------------------------------------------------------------

TEST_F(DescriptorManagerTest, CalculatePoolSizes_SingleLayout)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .AddBinding(1, vk::DescriptorType::eCombinedImageSampler,
	                                vk::ShaderStageFlagBits::eFragment)
	                    .Build();

	DescriptorSetLayout layout(*m_device, bindings);
	std::vector<const DescriptorSetLayout*> layouts = {&layout};

	auto sizes = DescriptorPool::CalculatePoolSizes(layouts, 2);

	// We expect: UBO: 2 (1 × 2), CombinedImageSampler: 2 (1 × 2)
	bool foundUbo = false;
	bool foundSampler = false;
	for (const auto& s : sizes)
	{
		if (s.type == vk::DescriptorType::eUniformBuffer)
		{
			EXPECT_GE(s.descriptorCount, 2u);
			foundUbo = true;
		}
		if (s.type == vk::DescriptorType::eCombinedImageSampler)
		{
			EXPECT_GE(s.descriptorCount, 2u);
			foundSampler = true;
		}
	}
	EXPECT_TRUE(foundUbo);
	EXPECT_TRUE(foundSampler);
}

TEST_F(DescriptorManagerTest, CalculatePoolSizes_MultipleLayouts)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindingsA = BuildLayout()
	                     .AddBinding(0,
	                                 vk::DescriptorType::eUniformBuffer,
	                                 vk::ShaderStageFlagBits::eVertex)
	                     .Build();

	auto bindingsB = BuildLayout()
	                     .AddBinding(0,
	                                 vk::DescriptorType::eUniformBuffer,
	                                 vk::ShaderStageFlagBits::eFragment)
	                     .AddBinding(1,
	                                 vk::DescriptorType::eStorageBuffer,
	                                 vk::ShaderStageFlagBits::eFragment)
	                     .Build();

	DescriptorSetLayout layoutA(*m_device, bindingsA);
	DescriptorSetLayout layoutB(*m_device, bindingsB);
	std::vector<const DescriptorSetLayout*> layouts = {&layoutA, &layoutB};

	auto sizes = DescriptorPool::CalculatePoolSizes(layouts, 3);

	// UBO appears in both layouts: (1+1)×3 = 6
	bool foundUbo = false;
	for (const auto& s : sizes)
	{
		if (s.type == vk::DescriptorType::eUniformBuffer)
		{
			EXPECT_GE(s.descriptorCount, 6u);
			foundUbo = true;
		}
	}
	EXPECT_TRUE(foundUbo);
}

// ---------------------------------------------------------------------------
// DescriptorSet allocation
// ---------------------------------------------------------------------------

TEST_F(DescriptorManagerTest, Allocate_SingleDescriptorSet_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	DescriptorSetLayout layout(*m_device, bindings);

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{vk::DescriptorType::eUniformBuffer, 1},
	};
	DescriptorPool pool(*m_device, 1, poolSizes);

	auto sets = pool.Allocate(layout, 1);
	ASSERT_EQ(sets.size(), 1u);

	// Handle must be valid (non-null)
	EXPECT_NE(sets[0].handle(), vk::DescriptorSet{});
}

TEST_F(DescriptorManagerTest, Allocate_MultipleDescriptorSets_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	DescriptorSetLayout layout(*m_device, bindings);

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{vk::DescriptorType::eUniformBuffer, 3},
	};
	DescriptorPool pool(*m_device, 3, poolSizes);

	auto sets = pool.Allocate(layout, 3);
	ASSERT_EQ(sets.size(), 3u);

	// All handles must be non-null and unique
	EXPECT_NE(sets[0].handle(), vk::DescriptorSet{});
	EXPECT_NE(sets[1].handle(), vk::DescriptorSet{});
	EXPECT_NE(sets[2].handle(), vk::DescriptorSet{});
	EXPECT_NE(sets[0].handle(), sets[1].handle());
	EXPECT_NE(sets[1].handle(), sets[2].handle());
	EXPECT_NE(sets[0].handle(), sets[2].handle());
}

TEST_F(DescriptorManagerTest, Allocate_ZeroCount_ReturnsEmpty)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	DescriptorSetLayout layout(*m_device, bindings);

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{vk::DescriptorType::eUniformBuffer, 1},
	};
	DescriptorPool pool(*m_device, 1, poolSizes);

	auto sets = pool.Allocate(layout, 0);
	EXPECT_TRUE(sets.empty());
}

// ---------------------------------------------------------------------------
// DescriptorSet write operations
// ---------------------------------------------------------------------------

TEST_F(DescriptorManagerTest, WriteBuffer_OnAllocatedSet_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create a host-visible uniform buffer
	constexpr vk::DeviceSize kBufSize = 256;
	VulkanBuffer buf(*m_device,
	                 (*m_physicalDevices)[0],
	                 m_queue,
	                 m_queueFamilyIndex,
	                 kBufSize,
	                 vk::BufferUsageFlagBits::eUniformBuffer,
	                 vk::MemoryPropertyFlagBits::eHostVisible |
	                     vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::DescriptorBufferInfo bufInfo = buf.GetDescriptorInfo();

	// Create layout, pool, allocate set
	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	DescriptorSetLayout layout(*m_device, bindings);

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{vk::DescriptorType::eUniformBuffer, 1},
	};
	DescriptorPool pool(*m_device, 1, poolSizes);

	auto sets = pool.Allocate(layout, 1);
	ASSERT_EQ(sets.size(), 1u);

	// Write the buffer descriptor — must not throw
	ASSERT_NO_THROW({
		sets[0].WriteBuffer(0, bufInfo,
		                    vk::DescriptorType::eUniformBuffer);
	});
}

TEST_F(DescriptorManagerTest, WriteBuffer_DefaultType_UniformBuffer)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kBufSize = 128;
	VulkanBuffer buf(*m_device,
	                 (*m_physicalDevices)[0],
	                 m_queue,
	                 m_queueFamilyIndex,
	                 kBufSize,
	                 vk::BufferUsageFlagBits::eStorageBuffer,
	                 vk::MemoryPropertyFlagBits::eHostVisible |
	                     vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::DescriptorBufferInfo bufInfo = buf.GetDescriptorInfo();

	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eStorageBuffer,
	                                vk::ShaderStageFlagBits::eCompute)
	                    .Build();

	DescriptorSetLayout layout(*m_device, bindings);

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		{vk::DescriptorType::eStorageBuffer, 1},
	};
	DescriptorPool pool(*m_device, 1, poolSizes);

	auto sets = pool.Allocate(layout, 1);
	ASSERT_EQ(sets.size(), 1u);

	// Default type should be UniformBuffer; we override to StorageBuffer
	ASSERT_NO_THROW({
		sets[0].WriteBuffer(0, bufInfo,
		                    vk::DescriptorType::eStorageBuffer);
	});
}

// ---------------------------------------------------------------------------
// Standard layout patterns (demonstrated through tests per spec)
// ---------------------------------------------------------------------------

TEST_F(DescriptorManagerTest, StandardLayout_CameraUBO)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Camera UBO: set 0, binding 0 — uniform buffer, vertex stage
	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eVertex)
	                    .Build();

	ASSERT_EQ(bindings.size(), 1u);
	EXPECT_EQ(bindings[0].binding, 0u);
	EXPECT_EQ(bindings[0].descriptorType, vk::DescriptorType::eUniformBuffer);

	DescriptorSetLayout layout(*m_device, bindings);
	EXPECT_EQ(layout.bindings().size(), 1u);
}

TEST_F(DescriptorManagerTest, StandardLayout_MaterialUBOAndTexture)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Material UBO (set 1, binding 0) + texture sampler (set 1, binding 1+)
	auto bindings = BuildLayout()
	                    .AddBinding(0, vk::DescriptorType::eUniformBuffer,
	                                vk::ShaderStageFlagBits::eFragment)
	                    .AddBinding(1,
	                                vk::DescriptorType::eCombinedImageSampler,
	                                vk::ShaderStageFlagBits::eFragment)
	                    .AddBinding(2,
	                                vk::DescriptorType::eCombinedImageSampler,
	                                vk::ShaderStageFlagBits::eFragment)
	                    .Build();

	ASSERT_EQ(bindings.size(), 3u);
	EXPECT_EQ(bindings[0].descriptorType, vk::DescriptorType::eUniformBuffer);
	EXPECT_EQ(bindings[1].descriptorType,
	          vk::DescriptorType::eCombinedImageSampler);
	EXPECT_EQ(bindings[2].descriptorType,
	          vk::DescriptorType::eCombinedImageSampler);

	DescriptorSetLayout layout(*m_device, bindings);
	EXPECT_EQ(layout.bindings().size(), 3u);
}
