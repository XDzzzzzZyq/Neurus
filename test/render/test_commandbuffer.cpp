// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "render/CommandBuffer.h"
#include "render/VulkanContext.h"

using namespace neurus;

/**
 * @brief Tests for CommandBuffer — RAII wrapper around vk::raii::CommandBuffer.
 *
 * Creates Vulkan instance, device, and command pool (no surface needed for
 * command buffer operations).
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class CommandBufferTest : public ::testing::Test
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

			// --- Find a queue family with graphics bit ---
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

			// --- Create command pool ---
			vk::CommandPoolCreateInfo poolInfo(
				vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				m_queueFamilyIndex);
			m_commandPool = std::make_unique<vk::raii::CommandPool>(*m_device, poolInfo);

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
		m_commandPool.reset();
		m_device.reset();
		m_physicalDevices.reset();
		m_instance.reset();
	}

	std::unique_ptr<vk::raii::Instance> m_instance;
	std::unique_ptr<vk::raii::PhysicalDevices> m_physicalDevices;
	std::unique_ptr<vk::raii::Device> m_device;
	std::unique_ptr<vk::raii::CommandPool> m_commandPool;
	vk::Queue m_queue = nullptr;
	uint32_t m_queueFamilyIndex = 0;
	bool m_hasVulkan = false;
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, Create_HandleIsValid)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_THROW({
		CommandBuffer cmdBuf(*m_device, *m_commandPool);
		EXPECT_NE(cmdBuf.handle(), vk::CommandBuffer{});
	});
}

TEST_F(CommandBufferTest, Create_WithPrimaryLevel_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_THROW({
		CommandBuffer cmdBuf(*m_device, *m_commandPool, vk::CommandBufferLevel::ePrimary);
		EXPECT_NE(cmdBuf.handle(), vk::CommandBuffer{});
	});
}

// ---------------------------------------------------------------------------
// Begin / End lifecycle
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, BeginEnd_Lifecycle)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);

	EXPECT_FALSE(cmdBuf.isRecording());
	ASSERT_NO_THROW(cmdBuf.Begin());
	EXPECT_TRUE(cmdBuf.isRecording());
	ASSERT_NO_THROW(cmdBuf.End());
	EXPECT_FALSE(cmdBuf.isRecording());
}

// ---------------------------------------------------------------------------
// Submit
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, Submit_ToGraphicsQueue_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();
	cmdBuf.End();

	ASSERT_NO_THROW(cmdBuf.Submit(m_queue));

	// Wait for GPU to finish
	m_device->waitIdle();
}

TEST_F(CommandBufferTest, Submit_WithFence_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();
	cmdBuf.End();

	vk::raii::Fence fence(*m_device, vk::FenceCreateInfo());

	ASSERT_NO_THROW(cmdBuf.Submit(m_queue, *fence));

	// Wait with timeout
	vk::Result waitResult = m_device->waitForFences(*fence, VK_TRUE, UINT64_MAX);
	EXPECT_EQ(waitResult, vk::Result::eSuccess);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, Reset_AfterBeginEnd_Succeeds)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);

	cmdBuf.Begin();
	cmdBuf.End();

	ASSERT_NO_THROW(cmdBuf.Reset());
	EXPECT_FALSE(cmdBuf.isRecording());
}

// ---------------------------------------------------------------------------
// Full lifecycle cycle
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, BeginEndReset_FullCycle)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);

	for (int i = 0; i < 3; ++i)
	{
		ASSERT_NO_THROW(cmdBuf.Begin());
		EXPECT_TRUE(cmdBuf.isRecording());
		ASSERT_NO_THROW(cmdBuf.End());
		EXPECT_FALSE(cmdBuf.isRecording());

		ASSERT_NO_THROW(cmdBuf.Reset());
	}
}

TEST_F(CommandBufferTest, BeginSubmitReset_FullCycle)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);

	for (int i = 0; i < 3; ++i)
	{
		cmdBuf.Begin();
		cmdBuf.End();
		cmdBuf.Submit(m_queue);
		m_device->waitIdle();
		cmdBuf.Reset();
	}

	// Re-record after cycle
	cmdBuf.Begin();
	cmdBuf.End();
	cmdBuf.Submit(m_queue);
	m_device->waitIdle();

	SUCCEED();
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, MoveConstructor_TransfersOwnership)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBufA(*m_device, *m_commandPool);
	vk::CommandBuffer handleA = cmdBufA.handle();

	// Begin/End/Submit on original
	cmdBufA.Begin();
	cmdBufA.End();
	cmdBufA.Submit(m_queue);
	m_device->waitIdle();

	// Move-construct
	CommandBuffer cmdBufB(std::move(cmdBufA));

	// cmdBufB should now own the resource
	EXPECT_EQ(cmdBufB.handle(), handleA);

	// cmdBufB should be usable
	ASSERT_NO_THROW(cmdBufB.Begin());
	ASSERT_NO_THROW(cmdBufB.End());
	ASSERT_NO_THROW(cmdBufB.Submit(m_queue));
	m_device->waitIdle();
}

// ---------------------------------------------------------------------------
// State-only recording methods
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, SetViewport_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	vk::Viewport vp(0.0f, 0.0f, 256.0f, 256.0f, 0.0f, 1.0f);
	ASSERT_NO_THROW(cmdBuf.SetViewport(0, vp));

	cmdBuf.End();
}

TEST_F(CommandBufferTest, SetScissor_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	vk::Rect2D scissor({0, 0}, {256, 256});
	ASSERT_NO_THROW(cmdBuf.SetScissor(0, scissor));

	cmdBuf.End();
}

// ---------------------------------------------------------------------------
// Copy operations
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, CopyBuffer_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create source and destination buffers
	vk::BufferCreateInfo bufInfo(
		{},
		64,
		vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
		vk::SharingMode::eExclusive);

	vk::raii::Buffer srcBuf(*m_device, bufInfo);
	vk::raii::Buffer dstBuf(*m_device, bufInfo);

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	vk::BufferCopy region(0, 0, 64);
	ASSERT_NO_THROW(cmdBuf.CopyBuffer(*srcBuf, *dstBuf, region));

	cmdBuf.End();
}

TEST_F(CommandBufferTest, CopyBufferToImage_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create a buffer with transfer source usage
	vk::BufferCreateInfo bufInfo(
		{},
		1024,
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::SharingMode::eExclusive);
	vk::raii::Buffer srcBuf(*m_device, bufInfo);

	// Create a 2D color image
	vk::ImageCreateInfo imgInfo(
		{},
		vk::ImageType::e2D,
		vk::Format::eR8G8B8A8Unorm,
		vk::Extent3D(16, 16, 1),
		1, 1,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst,
		vk::SharingMode::eExclusive);
	vk::raii::Image dstImg(*m_device, imgInfo);

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	vk::BufferImageCopy copyRegion(
		0, 0, 0,
		vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
		{0, 0, 0},
		{16, 16, 1});
	ASSERT_NO_THROW(cmdBuf.CopyBufferToImage(
		*srcBuf, *dstImg, vk::ImageLayout::eTransferDstOptimal, copyRegion));

	cmdBuf.End();
}

// ---------------------------------------------------------------------------
// Pipeline barrier
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, PipelineBarrier_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	ASSERT_NO_THROW(cmdBuf.PipelineBarrier(
		vk::PipelineStageFlagBits::eTopOfPipe,
		vk::PipelineStageFlagBits::eBottomOfPipe,
		{},
		{},    // memory barriers
		{},    // buffer barriers
		{}    // image barriers
	));

	cmdBuf.End();
}

// ---------------------------------------------------------------------------
// Draw / Dispatch (just record, no actual execution requires pipeline)
// ---------------------------------------------------------------------------

TEST_F(CommandBufferTest, Draw_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	ASSERT_NO_THROW(cmdBuf.Draw(3, 1, 0, 0));

	cmdBuf.End();
}

TEST_F(CommandBufferTest, DrawIndexed_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	ASSERT_NO_THROW(cmdBuf.DrawIndexed(3, 1, 0, 0, 0));

	cmdBuf.End();
}

TEST_F(CommandBufferTest, PushConstants_NoErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Pipeline layout with push constant range
	vk::PushConstantRange pcRange(vk::ShaderStageFlagBits::eVertex, 0, 16);
	vk::PipelineLayoutCreateInfo layoutInfo({}, {}, pcRange);
	vk::raii::PipelineLayout layout(*m_device, layoutInfo);

	CommandBuffer cmdBuf(*m_device, *m_commandPool);
	cmdBuf.Begin();

	struct PushData
	{
		float values[4];
	};
	PushData data = {{1.0f, 2.0f, 3.0f, 4.0f}};

	EXPECT_NO_THROW(cmdBuf.PushConstants(
		*layout,
		vk::ShaderStageFlagBits::eVertex,
		0,
		sizeof(PushData),
		&data));

	cmdBuf.End();
}
