// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include "render/AttachmentManager.h"
#include "render/VulkanImage.h"

#include <vulkan/vulkan_raii.hpp>

using namespace neurus;

/**
 * @brief Tests for AttachmentManager - G-Buffer and post-FX attachment creation, resize, and named access.
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class AttachmentManagerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Instance ---
			vk::ApplicationInfo appInfo("NeurusTest_Attach", VK_MAKE_VERSION(0, 1, 0),
			                            "NeurusTest_Attach", VK_MAKE_VERSION(0, 1, 0),
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

	bool m_hasVulkan = false;

	vk::raii::Context m_context;
	std::unique_ptr<vk::raii::Instance> m_instance;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;
	uint32_t m_selectedPdIndex = 0;
	std::unique_ptr<vk::raii::Device> m_device;
	uint32_t m_graphicsQueueFamily = 0;
};

// ---------------------------------------------------------------------------
// Creation - G-Buffer attachments
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, CreateGBuffer_AllAttachmentsHaveCorrectFormatAndExtent)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(1920, 1080);
	auto& pd = m_physicalDevices[m_selectedPdIndex];

	// Depth format support check
	auto depthFmtProps = pd.getFormatProperties(vk::Format::eD32Sfloat);
	if (!(depthFmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
	{
		GTEST_SKIP() << "D32_SFLOAT depth attachment not supported.";
	}

	AttachmentManager manager(*m_device, pd);
	manager.Create(extent);

	// --- Position (RGBA16_SFLOAT) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::Position);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eR16G16B16A16Sfloat);
		EXPECT_EQ(attachment.MipLevels(), 1u);
		EXPECT_EQ(attachment.ArrayLayers(), 1u);
		EXPECT_NE(attachment.Type(), VulkanImage::ImageType::eDepthStencil);
	}

	// --- Normal (RGBA16_SFLOAT) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::Normal);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eR16G16B16A16Sfloat);
		EXPECT_EQ(attachment.MipLevels(), 1u);
	}

	// --- Albedo (RGBA8_SRGB) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::Albedo);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eR8G8B8A8Srgb);
		EXPECT_EQ(attachment.MipLevels(), 1u);
	}

	// --- MetallicRoughness (RGBA8_UNORM) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::MetallicRoughness);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eR8G8B8A8Unorm);
		EXPECT_EQ(attachment.MipLevels(), 1u);
	}

	// --- Depth (D32_SFLOAT) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::Depth);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eD32Sfloat);
		EXPECT_EQ(attachment.MipLevels(), 1u);
		EXPECT_EQ(attachment.Type(), VulkanImage::ImageType::eDepthStencil);
	}
}

// ---------------------------------------------------------------------------
// Creation - Post-FX attachments
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, CreatePostFX_AllAttachmentsHaveCorrectFormatAndExtent)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(1920, 1080);
	auto& pd = m_physicalDevices[m_selectedPdIndex];

	AttachmentManager manager(*m_device, pd);
	manager.Create(extent);

	// --- HDRColor (RGBA16F) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::HDRColor);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eR16G16B16A16Sfloat);
		EXPECT_EQ(attachment.MipLevels(), 1u);
	}

	// --- SSAO (R8) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::SSAO);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eR8Unorm);
		EXPECT_EQ(attachment.MipLevels(), 1u);
	}

	// --- SSR (RGBA16F) ---
	{
		const auto& attachment = manager.GetAttachment(AttachmentName::SSR);
		EXPECT_EQ(attachment.Extent(), extent);
		EXPECT_EQ(attachment.Format(), vk::Format::eR16G16B16A16Sfloat);
		EXPECT_EQ(attachment.MipLevels(), 1u);
	}
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, Resize_AllAttachmentsHaveNewExtent)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	auto depthFmtProps = pd.getFormatProperties(vk::Format::eD32Sfloat);
	if (!(depthFmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
	{
		GTEST_SKIP() << "D32_SFLOAT depth attachment not supported.";
	}

	const vk::Extent2D initialExtent(1920, 1080);
	const vk::Extent2D newExtent(3840, 2160);

	AttachmentManager manager(*m_device, pd);
	manager.Create(initialExtent);

	// All attachments have initial extent
	EXPECT_EQ(manager.GetAttachment(AttachmentName::Position).Extent(), initialExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::Depth).Extent(), initialExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::HDRColor).Extent(), initialExtent);

	manager.Resize(newExtent);

	// All attachments have new extent
	EXPECT_EQ(manager.GetAttachment(AttachmentName::Position).Extent(), newExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::Normal).Extent(), newExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::Albedo).Extent(), newExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::MetallicRoughness).Extent(), newExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::Depth).Extent(), newExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::HDRColor).Extent(), newExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::SSAO).Extent(), newExtent);
	EXPECT_EQ(manager.GetAttachment(AttachmentName::SSR).Extent(), newExtent);
}

// ---------------------------------------------------------------------------
// Named access - all attachment names reachable
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, GetAllAttachments_ByEnumAndString)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	auto depthFmtProps = pd.getFormatProperties(vk::Format::eD32Sfloat);
	if (!(depthFmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
	{
		GTEST_SKIP() << "D32_SFLOAT depth attachment not supported.";
	}

	const vk::Extent2D extent(1280, 720);
	AttachmentManager manager(*m_device, pd);
	manager.Create(extent);

	// G-Buffer
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::Position).ImageHandle());
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::Normal).ImageHandle());
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::Albedo).ImageHandle());
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::MetallicRoughness).ImageHandle());
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::Depth).ImageHandle());

	// Post-FX
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::HDRColor).ImageHandle());
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::SSAO).ImageHandle());
	EXPECT_TRUE(*manager.GetAttachment(AttachmentName::SSR).ImageHandle());
}

// ---------------------------------------------------------------------------
// Non-copyable, movable
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<AttachmentManager>,
	              "AttachmentManager must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<AttachmentManager>,
	              "AttachmentManager must not be copy-assignable");
	SUCCEED();
}

TEST_F(AttachmentManagerTest, Movable)
{
	static_assert(std::is_move_constructible_v<AttachmentManager>,
	              "AttachmentManager must be move-constructible");
	static_assert(std::is_move_assignable_v<AttachmentManager>,
	              "AttachmentManager must be move-assignable");
	SUCCEED();
}

// ---------------------------------------------------------------------------
// HasAttachment check
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, HasAttachment_TrueForCreatedAttachments)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	auto depthFmtProps = pd.getFormatProperties(vk::Format::eD32Sfloat);
	if (!(depthFmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
	{
		GTEST_SKIP() << "D32_SFLOAT depth attachment not supported.";
	}

	const vk::Extent2D extent(800, 600);
	AttachmentManager manager(*m_device, pd);
	manager.Create(extent);

	EXPECT_TRUE(manager.HasAttachment(AttachmentName::Position));
	EXPECT_TRUE(manager.HasAttachment(AttachmentName::Depth));
	EXPECT_TRUE(manager.HasAttachment(AttachmentName::HDRColor));
	EXPECT_TRUE(manager.HasAttachment(AttachmentName::SSAO));
}
