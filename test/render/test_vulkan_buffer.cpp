// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "render/VulkanBuffer.h"
#include "render/VulkanContext.h"

using namespace neurus;

/**
 * @brief Tests for VulkanBuffer — RAII buffer + staging upload.
 *
 * Creates Vulkan instance and device without a surface (graphics queue
 * without present support is sufficient for buffer operations).
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class VulkanBufferTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Create instance ---
			m_instance = std::make_unique<vk::raii::Instance>(VulkanContext::CreateInstance());

			// --- Enumerate physical devices ---
			m_physicalDevices = std::make_unique<vk::raii::PhysicalDevices>(*m_instance);
			if (m_physicalDevices->empty())
			{
				m_hasVulkan = false;
				return;
			}

			// --- Find a queue family with graphics bit (no surface needed) ---
			auto qfProps = (*m_physicalDevices)[0].getQueueFamilyProperties();
			m_queueFamilyIndex = UINT32_MAX;
			for (uint32_t i = 0; i < static_cast<uint32_t>(qfProps.size()); ++i)
			{
				if (qfProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
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
			vk::DeviceQueueCreateInfo qCI({}, m_queueFamilyIndex, 1, &prio);
			vk::DeviceCreateInfo devCI({}, qCI);

			m_device = std::make_unique<vk::raii::Device>(
				(*m_physicalDevices)[0], devCI);

			m_queue = m_device->getQueue(m_queueFamilyIndex, 0);
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
		// vk::raii handles cleanup in reverse order
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
// Construction
// ---------------------------------------------------------------------------

TEST_F(VulkanBufferTest, CreateHostVisibleBuffer_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 256;

	ASSERT_NO_THROW({
		VulkanBuffer buf(*m_device,
		                 (*m_physicalDevices)[0],
		                 m_queue,
		                 m_queueFamilyIndex,
		                 kSize,
		                 vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
		                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		EXPECT_EQ(buf.size(), kSize);
	});
}

TEST_F(VulkanBufferTest, CreateDeviceLocalBuffer_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 1024;

	ASSERT_NO_THROW({
		VulkanBuffer buf(*m_device,
		                 (*m_physicalDevices)[0],
		                 m_queue,
		                 m_queueFamilyIndex,
		                 kSize,
		                 vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
		                 vk::MemoryPropertyFlagBits::eDeviceLocal);
		EXPECT_EQ(buf.size(), kSize);
	});
}

// ---------------------------------------------------------------------------
// Upload + Map / Unmap (host-visible buffer)
// ---------------------------------------------------------------------------

TEST_F(VulkanBufferTest, UploadThenMap_DataRoundtrips)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 64;
	std::array<uint8_t, kSize> srcData;
	for (size_t i = 0; i < kSize; ++i)
	{
		srcData[i] = static_cast<uint8_t>(i * 7);
	}

	VulkanBuffer buf(*m_device,
	                 (*m_physicalDevices)[0],
	                 m_queue,
	                 m_queueFamilyIndex,
	                 kSize,
	                 vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	// Upload via staging
	ASSERT_NO_THROW(buf.Upload(srcData.data(), kSize));

	// Read back via map
	void* mapped = buf.Map();
	ASSERT_NE(mapped, nullptr);

	int cmp = std::memcmp(mapped, srcData.data(), kSize);
	EXPECT_EQ(cmp, 0) << "Data mismatch after staging upload + map read-back";

	buf.Unmap();
}

// ---------------------------------------------------------------------------
// Map + write + Unmap + Map + read (no staging — direct host-visible access)
// ---------------------------------------------------------------------------

TEST_F(VulkanBufferTest, MapWriteThenRead_DataRoundtrips)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 32;
	std::array<uint8_t, kSize> data;
	for (size_t i = 0; i < kSize; ++i)
	{
		data[i] = static_cast<uint8_t>(~i);
	}

	VulkanBuffer buf(*m_device,
	                 (*m_physicalDevices)[0],
	                 m_queue,
	                 m_queueFamilyIndex,
	                 kSize,
	                 vk::BufferUsageFlagBits::eUniformBuffer,
	                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	// Direct Map + write
	void* mapped = buf.Map();
	ASSERT_NE(mapped, nullptr);
	std::memcpy(mapped, data.data(), kSize);
	buf.Unmap();

	// Map again + read-back
	mapped = buf.Map();
	ASSERT_NE(mapped, nullptr);
	for (size_t i = 0; i < kSize; ++i)
	{
		EXPECT_EQ(static_cast<const uint8_t*>(mapped)[i], data[i])
			<< "Byte mismatch at offset " << i;
	}
	buf.Unmap();
}

// ---------------------------------------------------------------------------
// GetDescriptorInfo
// ---------------------------------------------------------------------------

TEST_F(VulkanBufferTest, GetDescriptorInfo_ReturnsValidInfo)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 128;

	VulkanBuffer buf(*m_device,
	                 (*m_physicalDevices)[0],
	                 m_queue,
	                 m_queueFamilyIndex,
	                 kSize,
	                 vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::DescriptorBufferInfo info = buf.GetDescriptorInfo();

	// The buffer handle must be valid (non-null)
	EXPECT_NE(info.buffer, vk::Buffer{});
	EXPECT_EQ(info.offset, 0u);
	EXPECT_EQ(info.range, kSize);

	// With explicit offset and range
	vk::DescriptorBufferInfo partialInfo = buf.GetDescriptorInfo(16, 32);
	EXPECT_EQ(partialInfo.offset, 16u);
	EXPECT_EQ(partialInfo.range, 32u);
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST_F(VulkanBufferTest, MoveConstructor_TransfersOwnership)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 64;

	VulkanBuffer bufA(*m_device,
	                  (*m_physicalDevices)[0],
	                  m_queue,
	                  m_queueFamilyIndex,
	                  kSize,
	                  vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::Buffer origHandle = bufA.buffer();

	// Move-construct
	VulkanBuffer bufB(std::move(bufA));

	// bufB should now own the resource
	EXPECT_EQ(bufB.size(), kSize);
	EXPECT_EQ(bufB.buffer(), origHandle);
}

TEST_F(VulkanBufferTest, MoveAssignment_TransfersOwnership)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 64;

	VulkanBuffer bufA(*m_device,
	                  (*m_physicalDevices)[0],
	                  m_queue,
	                  m_queueFamilyIndex,
	                  kSize,
	                  vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::Buffer origHandle = bufA.buffer();

	// Move-assign
	VulkanBuffer bufB(*m_device,
	                  (*m_physicalDevices)[0],
	                  m_queue,
	                  m_queueFamilyIndex,
	                  32,  // different size
	                  vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                  vk::MemoryPropertyFlagBits::eDeviceLocal);

	bufB = std::move(bufA);

	EXPECT_EQ(bufB.size(), kSize);
	EXPECT_EQ(bufB.buffer(), origHandle);
}

// ---------------------------------------------------------------------------
// buffer() accessor
// ---------------------------------------------------------------------------

TEST_F(VulkanBufferTest, BufferAccessor_ReturnsValidHandle)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr vk::DeviceSize kSize = 16;

	VulkanBuffer buf(*m_device,
	                 (*m_physicalDevices)[0],
	                 m_queue,
	                 m_queueFamilyIndex,
	                 kSize,
	                 vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	EXPECT_NE(buf.buffer(), vk::Buffer{});
	EXPECT_EQ(buf.size(), kSize);
}
