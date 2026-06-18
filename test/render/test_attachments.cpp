#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "render/AttachmentManager.h"
#include "render/Image.h"

using namespace neurus;

/**
 * @brief Tests for AttachmentManager - G-Buffer and post-FX attachment creation, resize, and named access.
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class AttachmentManagerTest : public VulkanTestShared
{
protected:
	// SetUp/TearDown inherited from VulkanTestShared
};

// ---------------------------------------------------------------------------
// Creation - G-Buffer attachments
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, CreateGBuffer_AllAttachmentsHaveCorrectFormatAndExtent)
{
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(1920, 1080);
	auto& pd = PhysicalDevice();

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
		EXPECT_NE(attachment.Type(), Image::ImageType::eDepthStencil);
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
		EXPECT_EQ(attachment.Type(), Image::ImageType::eDepthStencil);
	}
}

// ---------------------------------------------------------------------------
// Creation - Post-FX attachments
// ---------------------------------------------------------------------------

TEST_F(AttachmentManagerTest, CreatePostFX_AllAttachmentsHaveCorrectFormatAndExtent)
{
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(1920, 1080);
	auto& pd = PhysicalDevice();

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
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

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
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

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
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

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
