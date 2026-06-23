// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include "shared/TestVulkanShared.h"

#include <gtest/gtest.h>

#include "render/Image.h"

#include <vulkan/vulkan_raii.hpp>

using namespace neurus;

/**
 * @brief Tests for Image - RAII image creation, layout transition, and mipmap generation.
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class ImageTest : public VulkanTestShared
{
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_F(ImageTest, Create2D_ValidHandles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(256, 256);
	auto& pd = PhysicalDevice();

	Image image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
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

TEST_F(ImageTest, Create2D_WithMipLevels)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const vk::Extent2D extent(512, 512);
	const uint32_t mipLevels = 4;
	auto& pd = PhysicalDevice();

	Image image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  mipLevels);

	EXPECT_TRUE(*image.ImageHandle());
	EXPECT_EQ(image.MipLevels(), mipLevels);
}

TEST_F(ImageTest, CreateCube_ValidHandles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();
	const vk::Extent2D extent(128, 128);

	Image image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	                  1,
	                  Image::ImageType::eCube);

	EXPECT_EQ(image.ArrayLayers(), 6u);
}

TEST_F(ImageTest, CreateDepthStencil_ValidHandles)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();
	const vk::Extent2D extent(1024, 768);

	// Check if depth format is supported
	auto fmtProps = pd.getFormatProperties(vk::Format::eD32Sfloat);
	if (!(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
	{
		GTEST_SKIP() << "D32_SFLOAT depth attachment not supported.";
	}

	Image image(*m_device, pd, extent, vk::Format::eD32Sfloat,
	                  vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                      vk::ImageUsageFlagBits::eSampled,
	                  1,
	                  Image::ImageType::eDepthStencil);

	EXPECT_TRUE(*image.ImageHandle());
	EXPECT_TRUE(*image.ImageViewHandle());
	EXPECT_TRUE(*image.DeviceMemoryHandle());
	EXPECT_EQ(image.Format(), vk::Format::eD32Sfloat);
}

// ---------------------------------------------------------------------------
// Layout transitions
// ---------------------------------------------------------------------------

TEST_F(ImageTest, TransitionLayout_Basic)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();
	const vk::Extent2D extent(64, 64);

	Image image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eColorAttachment |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  1);

	{
		auto& cmd = BeginCmd();
		// UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
		image.TransitionLayout(cmd, vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eColorAttachmentOptimal);

		// COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
		image.TransitionLayout(cmd, vk::ImageLayout::eColorAttachmentOptimal,
		                       vk::ImageLayout::eShaderReadOnlyOptimal);
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

TEST_F(ImageTest, TransitionLayout_MipLevels)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();
	const vk::Extent2D extent(256, 256);

	Image image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  4);

	{
		auto& cmd = BeginCmd();
		// Transition all 4 mip levels
		image.TransitionLayout(cmd,
		                       vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eTransferDstOptimal,
		                       0, 4);  // baseMip=0, levelCount=4
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

TEST_F(ImageTest, TransitionLayout_SingleMipLevel)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();
	const vk::Extent2D extent(128, 128);

	Image image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  4);

	{
		auto& cmd = BeginCmd();
		// Transition only mip level 1
		image.TransitionLayout(cmd,
		                       vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eTransferSrcOptimal,
		                       1, 1);  // baseMip=1, levelCount=1
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// Mipmap generation
// ---------------------------------------------------------------------------

TEST_F(ImageTest, GenerateMipmaps_Completes)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// Check that blitting from a linear-tiled format is supported
	auto fmtProps = pd.getFormatProperties(vk::Format::eR8G8B8A8Unorm);
	if (!(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc) ||
	    !(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst))
	{
		GTEST_SKIP() << "RGBA8 blit not supported by GPU.";
	}

	const vk::Extent2D extent(256, 256);

	Image image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  4);

	{
		auto& cmd = BeginCmd();
		// First transition to TRANSFER_DST so GenerateMipmaps can blit into lower levels
		image.TransitionLayout(cmd,
		                       vk::ImageLayout::eUndefined,
		                       vk::ImageLayout::eTransferDstOptimal,
		                       0, image.MipLevels());

		image.GenerateMipmaps(cmd);
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// Non-copyable, movable
// ---------------------------------------------------------------------------

TEST_F(ImageTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<Image>,
	              "Image must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<Image>,
	              "Image must not be copy-assignable");
	SUCCEED();
}

TEST_F(ImageTest, Movable)
{
	static_assert(std::is_move_constructible_v<Image>,
	              "Image must be move-constructible");
	static_assert(std::is_move_assignable_v<Image>,
	              "Image must be move-assignable");
	SUCCEED();
}
