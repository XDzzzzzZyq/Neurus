// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include "render/SyncObjects.h"

#include <chrono>
#include <memory>
#include <vector>

using namespace neurus;

// ============================================================================
// Barrier helper tests - pure construction, no device required
// ============================================================================

/**
 * @test ImageBarrier constructs a valid vk::ImageMemoryBarrier2 with default params.
 */
TEST(SyncObjectsTest, ImageBarrier_DefaultParams)
{
	vk::Image dummyImage = VK_NULL_HANDLE;
	auto barrier = ImageBarrier(
		dummyImage,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eColorAttachmentOptimal);

	EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::eUndefined);
	EXPECT_EQ(barrier.newLayout, vk::ImageLayout::eColorAttachmentOptimal);
	EXPECT_EQ(barrier.srcStageMask, vk::PipelineStageFlagBits2::eTopOfPipe);
	EXPECT_EQ(barrier.dstStageMask, vk::PipelineStageFlagBits2::eBottomOfPipe);
	EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits2::eNone);
	EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits2::eNone);
	EXPECT_EQ(barrier.image, dummyImage);
}

/**
 * @test ImageBarrier accepts custom stage/access masks.
 */
TEST(SyncObjectsTest, ImageBarrier_CustomStages)
{
	vk::Image dummyImage = VK_NULL_HANDLE;
	auto barrier = ImageBarrier(
		dummyImage,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eShaderReadOnlyOptimal,
		vk::PipelineStageFlagBits2::eTransfer,
		vk::AccessFlagBits2::eTransferWrite,
		vk::PipelineStageFlagBits2::eFragmentShader,
		vk::AccessFlagBits2::eShaderRead);

	EXPECT_EQ(barrier.oldLayout, vk::ImageLayout::eUndefined);
	EXPECT_EQ(barrier.newLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
	EXPECT_EQ(barrier.srcStageMask, vk::PipelineStageFlagBits2::eTransfer);
	EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits2::eTransferWrite);
	EXPECT_EQ(barrier.dstStageMask, vk::PipelineStageFlagBits2::eFragmentShader);
	EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits2::eShaderRead);
}

/**
 * @test BufferBarrier constructs a valid vk::BufferMemoryBarrier2 with default params.
 */
TEST(SyncObjectsTest, BufferBarrier_DefaultParams)
{
	vk::Buffer dummyBuffer = VK_NULL_HANDLE;
	auto barrier = BufferBarrier(dummyBuffer, 0, VK_WHOLE_SIZE);

	EXPECT_EQ(barrier.buffer, dummyBuffer);
	EXPECT_EQ(barrier.offset, 0u);
	EXPECT_EQ(barrier.size, VK_WHOLE_SIZE);
	EXPECT_EQ(barrier.srcStageMask, vk::PipelineStageFlagBits2::eTopOfPipe);
	EXPECT_EQ(barrier.dstStageMask, vk::PipelineStageFlagBits2::eBottomOfPipe);
	EXPECT_EQ(barrier.srcAccessMask, vk::AccessFlagBits2::eNone);
	EXPECT_EQ(barrier.dstAccessMask, vk::AccessFlagBits2::eNone);
}

/**
 * @test BufferBarrier accepts custom pipeline stages and access masks.
 */
TEST(SyncObjectsTest, BufferBarrier_CustomStages)
{
	vk::Buffer dummyBuffer = VK_NULL_HANDLE;
	auto barrier = BufferBarrier(
		dummyBuffer, 256, 1024,
		vk::PipelineStageFlagBits2::eVertexShader,
		vk::AccessFlagBits2::eVertexAttributeRead,
		vk::PipelineStageFlagBits2::eComputeShader,
		vk::AccessFlagBits2::eShaderWrite);

	EXPECT_EQ(barrier.offset, 256u);
	EXPECT_EQ(barrier.size, 1024u);
	EXPECT_EQ(barrier.srcStageMask, vk::PipelineStageFlagBits2::eVertexShader);
	EXPECT_EQ(barrier.dstStageMask, vk::PipelineStageFlagBits2::eComputeShader);
}

// ============================================================================
// Device-dependent tests - require a Vulkan-capable GPU
// ============================================================================

class SyncObjectsDeviceTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			m_context = std::make_unique<vk::raii::Context>();

			vk::ApplicationInfo appInfo(
				"SyncObjectsTest", 1,
				"SyncObjectsTest", 1,
				VK_API_VERSION_1_4);
			vk::InstanceCreateInfo instCI({}, &appInfo);
			auto instance = vk::raii::Instance(*m_context, instCI);
			m_instance = std::make_unique<vk::raii::Instance>(std::move(instance));

			auto physicalDevices = vk::raii::PhysicalDevices(*m_instance);
			if (physicalDevices.empty())
			{
				m_hasVulkan = false;
				return;
			}

			auto& pd = physicalDevices[0];

			// Find a graphics queue family
			auto queueFamilies = pd.getQueueFamilyProperties();
			m_queueFamily = UINT32_MAX;
			for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilies.size()); ++i)
			{
				if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics)
				{
					m_queueFamily = i;
					break;
				}
			}

			if (m_queueFamily == UINT32_MAX)
			{
				m_hasVulkan = false;
				return;
			}

			float priority = 1.0f;
			vk::DeviceQueueCreateInfo qCI({}, m_queueFamily, 1, &priority);
			m_device = std::make_unique<vk::raii::Device>(pd, vk::DeviceCreateInfo({}, qCI));

			m_hasVulkan = true;
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		m_device.reset();
		m_instance.reset();
		m_context.reset();
	}

	bool m_hasVulkan = false;
	std::unique_ptr<vk::raii::Context> m_context;
	std::unique_ptr<vk::raii::Instance> m_instance;
	uint32_t m_queueFamily = 0;
	std::unique_ptr<vk::raii::Device> m_device;
};

/**
 * @test Default-constructed Fence (unsignaled) can be created without error.
 */
TEST_F(SyncObjectsDeviceTest, Fence_CreateDefault)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_THROW({
		Fence fence(*m_device);
		EXPECT_TRUE(*fence.handle());
	});
}

/**
 * @test Fence can be created in the signaled state.
 */
TEST_F(SyncObjectsDeviceTest, Fence_CreateSignaled)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_THROW({
		Fence fence(*m_device, vk::FenceCreateFlagBits::eSignaled);
		EXPECT_TRUE(*fence.handle());
	});
}

/**
 * @test WaitAndReset on a signaled fence returns true and resets the fence.
 *
 * After the first successful WaitAndReset the fence is unsignaled, so a second
 * call with zero timeout must return false.
 */
TEST_F(SyncObjectsDeviceTest, Fence_WaitAndReset_Signaled)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	Fence fence(*m_device, vk::FenceCreateFlagBits::eSignaled);

	// First wait - fence is signaled, should succeed immediately
	EXPECT_TRUE(fence.WaitAndReset(0));

	// Second wait - fence was reset to unsignaled, should time out immediately
	EXPECT_FALSE(fence.WaitAndReset(0));
}

/**
 * @test WaitAndReset on an unsignaled fence times out (returns false).
 */
TEST_F(SyncObjectsDeviceTest, Fence_WaitAndReset_Unsignaled)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	Fence fence(*m_device);  // unsignaled by default

	// Zero timeout: fence is not signaled, so wait should fail immediately
	EXPECT_FALSE(fence.WaitAndReset(0));
}

/**
 * @test WaitAndReset on a signaled fence completes in well under 10 ms.
 *
 * Validates the timing constraint: a pre-signaled fence must be observable
 * without blocking the CPU.
 */
TEST_F(SyncObjectsDeviceTest, Fence_WaitAndReset_Timing)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	Fence fence(*m_device, vk::FenceCreateFlagBits::eSignaled);

	auto start = std::chrono::steady_clock::now();
	bool result = fence.WaitAndReset(kDefaultFenceTimeoutNs);
	auto elapsed = std::chrono::steady_clock::now() - start;

	EXPECT_TRUE(result);
	auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
	EXPECT_LT(elapsedMs, 10) << "WaitAndReset on a signaled fence took " << elapsedMs << " ms";
}

/**
 * @test Semaphore can be created without error.
 */
TEST_F(SyncObjectsDeviceTest, Semaphore_Create)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_THROW({
		Semaphore semaphore(*m_device);
		EXPECT_TRUE(*semaphore.handle());
	});
}

/**
 * @test FrameSync constructs all three synchronization objects without error.
 */
TEST_F(SyncObjectsDeviceTest, FrameSync_Create)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_THROW({
		FrameSync fs(*m_device);
		EXPECT_TRUE(*fs.inFlight.handle());
		EXPECT_TRUE(*fs.imageAvailable.handle());
		EXPECT_TRUE(*fs.renderFinished.handle());
	});
}

/**
 * @test FrameSync fence is signaled by default, so WaitAndReset returns true.
 */
TEST_F(SyncObjectsDeviceTest, FrameSync_FenceInitiallySignaled)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	FrameSync fs(*m_device);
	EXPECT_TRUE(fs.inFlight.WaitAndReset(0));
}
