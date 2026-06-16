// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include "render/VulkanImage.h"

#include <vulkan/vulkan_raii.hpp>

using namespace neurus;

/**
 * @brief Tests for VulkanImage — RAII image creation, layout transition, and mipmap generation.
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class VulkanImageTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Instance ---
			vk::ApplicationInfo appInfo("NeurusTest", VK_MAKE_VERSION(0, 1, 0),
			                            "NeurusTest", VK_MAKE_VERSION(0, 1, 0),
			                            VK_API_VERSION_1_4);
			std::vector<const char*> instanceExts = {
				VK_KHR_SURFACE_EXTENSION_NAME,
				VK_KHR_WIN32_SURFACE_EXTENSION_NAME
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

			// Pick discrete GPU if available, otherwise first
			m_selectedPdIndex = 0;
			for (uint32_t i = 0; i < static_cast<uint32_t>(m_physicalDevices.size()); ++i)
			{
				if (m_physicalDevices[i].getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
				{
					m_selectedPdIndex = i;
					break;
				}
			}
			auto& pd = m_physicalDevices[m_selectedPdIndex];

			// --- Queue family ---
			auto qfProps = pd.getQueueFamilyProperties();
			bool foundGraphics = false;
			for (uint32_t i = 0; i < static_cast<uint32_t>(qfProps.size()); ++i)
			{
				if (qfProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
				{
					m_graphicsQueueFamily = i;
					foundGraphics = true;
					break;
				}
			}
			if (!foundGraphics)
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

			// --- Command pool ---
			vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			                                 m_graphicsQueueFamily);
			m_commandPool = std::make_unique<vk::raii::CommandPool>(*m_device, poolCI);

			// --- Command buffers ---
			vk::CommandBufferAllocateInfo allocInfo(*m_commandPool, vk::CommandBufferLevel::ePrimary, 1);
			m_commandBuffers = vk::raii::CommandBuffers(*m_device, allocInfo);

			m_hasVulkan = true;
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		// RAII handles cleanup in reverse declaration order
	}

	/** Helper: begin one-shot command buffer. */
	vk::raii::CommandBuffer& BeginCmd()
	{
		auto& cmd = m_commandBuffers[0];
		cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		return cmd;
	}

	/** Helper: end and submit one-shot command buffer, then wait. */
	void EndCmd(vk::raii::CommandBuffer& cmd)
	{
		cmd.end();
		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmd));
		auto queue = m_device->getQueue(m_graphicsQueueFamily, 0);
		queue.submit(submitInfo, nullptr);
		queue.waitIdle();
	}

	/** Shortcut: record commands inside a one-shot buffer. */
	template <typename F>
	void OneShotSubmit(F&& recordFn)
	{
		auto& cmd = BeginCmd();
		recordFn(cmd);
		EndCmd(cmd);
	}

	bool m_hasVulkan = false;

	vk::raii::Context m_context;
	std::unique_ptr<vk::raii::Instance> m_instance;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;
	uint32_t m_selectedPdIndex = 0;
	std::unique_ptr<vk::raii::Device> m_device;
	uint32_t m_graphicsQueueFamily = 0;
	std::unique_ptr<vk::raii::CommandPool> m_commandPool;
	vk::raii::CommandBuffers m_commandBuffers = nullptr;
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_F(VulkanImageTest, Create2D_ValidHandles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(256, 256);
	auto& pd = m_physicalDevices[m_selectedPdIndex];

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	                  1);

	EXPECT_TRUE(*image.ImageHandle());
	EXPECT_TRUE(*image.ImageViewHandle());
	EXPECT_TRUE(*image.DeviceMemoryHandle());
	EXPECT_EQ(image.Extent(), extent);
	EXPECT_EQ(image.Format(), vk::Format::eR8G8B8A8Unorm);
	EXPECT_EQ(image.MipLevels(), 1u);
	EXPECT_EQ(image.ArrayLayers(), 1u);
}

TEST_F(VulkanImageTest, Create2D_WithMipLevels)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(512, 512);
	const uint32_t mipLevels = 4;
	auto& pd = m_physicalDevices[m_selectedPdIndex];

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  mipLevels);

	EXPECT_TRUE(*image.ImageHandle());
	EXPECT_EQ(image.MipLevels(), mipLevels);
}

TEST_F(VulkanImageTest, CreateCube_ValidHandles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];
	const vk::Extent2D extent(128, 128);

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	                  1,
	                  VulkanImage::ImageType::eCube);

	EXPECT_EQ(image.ArrayLayers(), 6u);
}

TEST_F(VulkanImageTest, CreateDepthStencil_ValidHandles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];
	const vk::Extent2D extent(1024, 768);

	// Check if depth format is supported
	auto fmtProps = pd.getFormatProperties(vk::Format::eD32Sfloat);
	if (!(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
	{
		GTEST_SKIP() << "D32_SFLOAT depth attachment not supported.";
	}

	VulkanImage image(*m_device, pd, extent, vk::Format::eD32Sfloat,
	                  vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                      vk::ImageUsageFlagBits::eSampled,
	                  1,
	                  VulkanImage::ImageType::eDepthStencil);

	EXPECT_TRUE(*image.ImageHandle());
	EXPECT_TRUE(*image.ImageViewHandle());
	EXPECT_TRUE(*image.DeviceMemoryHandle());
	EXPECT_EQ(image.Format(), vk::Format::eD32Sfloat);
}

// ---------------------------------------------------------------------------
// Layout transitions
// ---------------------------------------------------------------------------

TEST_F(VulkanImageTest, TransitionLayout_Basic)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];
	const vk::Extent2D extent(64, 64);

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eColorAttachment |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  1);

	OneShotSubmit([&](vk::raii::CommandBuffer& cmd) {
		// UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
		image.TransitionLayout(cmd, vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eColorAttachmentOptimal);

		// COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
		image.TransitionLayout(cmd, vk::ImageLayout::eColorAttachmentOptimal,
		                       vk::ImageLayout::eShaderReadOnlyOptimal);
	});

	SUCCEED();
}

TEST_F(VulkanImageTest, TransitionLayout_MipLevels)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];
	const vk::Extent2D extent(256, 256);

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  4);

	OneShotSubmit([&](vk::raii::CommandBuffer& cmd) {
		// Transition all 4 mip levels
		image.TransitionLayout(cmd,
		                       vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eTransferDstOptimal,
		                       0, 4);  // baseMip=0, levelCount=4
	});

	SUCCEED();
}

TEST_F(VulkanImageTest, TransitionLayout_SingleMipLevel)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];
	const vk::Extent2D extent(128, 128);

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  4);

	OneShotSubmit([&](vk::raii::CommandBuffer& cmd) {
		// Transition only mip level 1
		image.TransitionLayout(cmd,
		                       vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eTransferSrcOptimal,
		                       1, 1);  // baseMip=1, levelCount=1
	});

	SUCCEED();
}

// ---------------------------------------------------------------------------
// Mipmap generation
// ---------------------------------------------------------------------------

TEST_F(VulkanImageTest, GenerateMipmaps_Completes)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	// Check that blitting from a linear-tiled format is supported
	auto fmtProps = pd.getFormatProperties(vk::Format::eR8G8B8A8Unorm);
	if (!(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc) ||
	    !(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst))
	{
		GTEST_SKIP() << "RGBA8 blit not supported by GPU.";
	}

	const vk::Extent2D extent(256, 256);

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  4);

	OneShotSubmit([&](vk::raii::CommandBuffer& cmd) {
		// First transition to TRANSFER_DST so GenerateMipmaps can blit into lower levels
		image.TransitionLayout(cmd,
		                       vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eTransferDstOptimal,
		                       0, image.MipLevels());

		image.GenerateMipmaps(cmd);
	});

	SUCCEED();
}

// ---------------------------------------------------------------------------
// Non-copyable, movable
// ---------------------------------------------------------------------------

TEST_F(VulkanImageTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<VulkanImage>,
	              "VulkanImage must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<VulkanImage>,
	              "VulkanImage must not be copy-assignable");
	SUCCEED();
}

TEST_F(VulkanImageTest, Movable)
{
	static_assert(std::is_move_constructible_v<VulkanImage>,
	              "VulkanImage must be move-constructible");
	static_assert(std::is_move_assignable_v<VulkanImage>,
	              "VulkanImage must be move-assignable");
	SUCCEED();
}
