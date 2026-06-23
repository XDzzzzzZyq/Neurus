// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "render/VulkanBuffer.h"
#include "shared/TestVulkanShared.h"

using namespace neurus;

/**
 * @brief Tests for VulkanBuffer - RAII buffer + staging upload.
 *
 * Creates Vulkan instance and device without a surface (graphics queue
 * without present support is sufficient for buffer operations).
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class VulkanBufferTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		VulkanTestShared::SetUp();
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}
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
		                 PhysicalDevice(),
		                 m_queue,
		                 m_graphicsQueueFamily,
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
		                 PhysicalDevice(),
		                 m_queue,
		                 m_graphicsQueueFamily,
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
	                 PhysicalDevice(),
	                 m_queue,
	                 m_graphicsQueueFamily,
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
// Map + write + Unmap + Map + read (no staging - direct host-visible access)
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
	                 PhysicalDevice(),
	                 m_queue,
	                 m_graphicsQueueFamily,
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
	                 PhysicalDevice(),
	                 m_queue,
	                 m_graphicsQueueFamily,
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
	                  PhysicalDevice(),
	                  m_queue,
	                  m_graphicsQueueFamily,
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
	                  PhysicalDevice(),
	                  m_queue,
	                  m_graphicsQueueFamily,
	                  kSize,
	                  vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::Buffer origHandle = bufA.buffer();

	// Move-assign
	VulkanBuffer bufB(*m_device,
	                  PhysicalDevice(),
	                  m_queue,
	                  m_graphicsQueueFamily,
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
	                 PhysicalDevice(),
	                 m_queue,
	                 m_graphicsQueueFamily,
	                 kSize,
	                 vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
	                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	EXPECT_NE(buf.buffer(), vk::Buffer{});
	EXPECT_EQ(buf.size(), kSize);
}
