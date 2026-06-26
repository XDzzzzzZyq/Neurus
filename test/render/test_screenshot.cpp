#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"
#include "render/Screenshot.h"
#include "render/Image.h"
#include "asset/ImageData.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace neurus;

/**
 * @brief Tests for Screenshot - GPU image capture to PNG via staging buffer.
 *
 * Creates an Image, uploads known pixel data, captures to PNG,
 * and verifies file existence and non-zero size.
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class ScreenshotTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		VulkanTestShared::SetUp();
	}

	void TearDown() override
	{
		// Clean up test output file
		if (!m_testOutputPath.empty() && std::filesystem::exists(m_testOutputPath))
		{
			std::filesystem::remove(m_testOutputPath);
		}
		VulkanTestShared::TearDown();
	}



	std::string m_testOutputPath;
};

// ---------------------------------------------------------------------------
// CaptureAttachment - RGBA8 image → PNG
// ---------------------------------------------------------------------------

TEST_F(ScreenshotTest, CaptureAttachment_RGBA8_WritesPngFile)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];
	const vk::Extent2D extent(64, 64);

	// --- Create an Image with red pixel data ---
	const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
	std::vector<uint8_t> redPixels(pixelCount * 4);
	for (size_t i = 0; i < pixelCount; ++i)
	{
		redPixels[i * 4 + 0] = 255;  // R
		redPixels[i * 4 + 1] = 0;    // G
		redPixels[i * 4 + 2] = 0;    // B
		redPixels[i * 4 + 3] = 255;  // A
	}
	ImageData imgData(redPixels.data(), extent.width, extent.height, vk::Format::eR8G8B8A8Unorm);
	auto imagePtr = Image::FromImageData(*m_device, pd, m_queue, m_graphicsQueueFamily, imgData);
	ASSERT_NE(imagePtr, nullptr);
	Image& image = *imagePtr;

	// --- Capture to PNG ---
	m_testOutputPath = "screenshots/test_rgba8.png";
	bool result = Screenshot::CaptureAttachment(*m_device, pd,
	                                             m_queue, m_graphicsQueueFamily,
	                                             image, m_testOutputPath);
	EXPECT_TRUE(result);

	// --- Verify file exists and is non-empty ---
	EXPECT_TRUE(std::filesystem::exists(m_testOutputPath));
	auto fileSize = std::filesystem::file_size(m_testOutputPath);
	EXPECT_GT(fileSize, 0u) << "PNG file should be non-empty";
}

// ---------------------------------------------------------------------------
// CaptureAttachment - RGBA16F image → PNG (half-float conversion)
// ---------------------------------------------------------------------------

TEST_F(ScreenshotTest, CaptureAttachment_RGBA16F_WritesPngFile)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	// Check RGBA16_SFLOAT format support
	auto fmtProps = pd.getFormatProperties(vk::Format::eR16G16B16A16Sfloat);
	if (!(fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferSrc))
	{
		GTEST_SKIP() << "RGBA16_SFLOAT not supported as transfer source on this GPU.";
	}

	const vk::Extent2D extent(32, 32);

	// --- Upload half-float data (0.5, 0.25, 0.75, 1.0) ---
	// Half-float 0.5 = 0x3800, 0.25 = 0x3400, 0.75 = 0x3A00, 1.0 = 0x3C00
	const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
	std::vector<uint16_t> halfData(pixelCount * 4);
	const uint16_t h0_5  = 0x3800;
	const uint16_t h0_25 = 0x3400;
	const uint16_t h0_75 = 0x3A00;
	const uint16_t h1_0  = 0x3C00;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		halfData[i * 4 + 0] = h0_5;
		halfData[i * 4 + 1] = h0_25;
		halfData[i * 4 + 2] = h0_75;
		halfData[i * 4 + 3] = h1_0;
	}
	ImageData imgData(halfData.data(), extent.width, extent.height, vk::Format::eR16G16B16A16Sfloat);
	auto imagePtr = Image::FromImageData(*m_device, pd, m_queue, m_graphicsQueueFamily, imgData);
	ASSERT_NE(imagePtr, nullptr);
	Image& image = *imagePtr;

	// --- Capture to PNG ---
	m_testOutputPath = "screenshots/test_rgba16f.png";
	bool result = Screenshot::CaptureAttachment(*m_device, pd,
	                                             m_queue, m_graphicsQueueFamily,
	                                             image, m_testOutputPath);
	EXPECT_TRUE(result);

	// --- Verify file exists and is non-empty ---
	EXPECT_TRUE(std::filesystem::exists(m_testOutputPath));
	auto fileSize = std::filesystem::file_size(m_testOutputPath);
	EXPECT_GT(fileSize, 0u) << "PNG file should be non-empty";
}

// ---------------------------------------------------------------------------
// CaptureAttachment - handles non-existent directory (auto-creation)
// ---------------------------------------------------------------------------

TEST_F(ScreenshotTest, CaptureAttachment_AutoCreatesDirectory)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];
	const vk::Extent2D extent(16, 16);

	const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
	std::vector<uint8_t> pixels(pixelCount * 4, 128);
	ImageData imgData(pixels.data(), extent.width, extent.height, vk::Format::eR8G8B8A8Unorm);
	auto imagePtr = Image::FromImageData(*m_device, pd, m_queue, m_graphicsQueueFamily, imgData);
	ASSERT_NE(imagePtr, nullptr);
	Image& image = *imagePtr;

	// --- Use a nested directory that doesn't exist ---
	const std::string nestedPath = "screenshots/nested/subdir/test_auto_dir.png";

	// Clean up potential leftover
	if (std::filesystem::exists("screenshots/nested"))
	{
		std::filesystem::remove_all("screenshots/nested");
	}

	m_testOutputPath = nestedPath;
	bool result = Screenshot::CaptureAttachment(*m_device, pd,
	                                             m_queue, m_graphicsQueueFamily,
	                                             image, nestedPath);
	EXPECT_TRUE(result);
	EXPECT_TRUE(std::filesystem::exists(nestedPath));
	auto fileSize = std::filesystem::file_size(nestedPath);
	EXPECT_GT(fileSize, 0u) << "PNG file should be non-empty";

	// Clean up nested test directory
	if (std::filesystem::exists("screenshots/nested"))
	{
		std::filesystem::remove_all("screenshots/nested");
		m_testOutputPath.clear();
	}
}

// ---------------------------------------------------------------------------
// CaptureAttachment - non-copyable class check
// ---------------------------------------------------------------------------

TEST_F(ScreenshotTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<Screenshot>,
	              "Screenshot must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<Screenshot>,
	              "Screenshot must not be copy-assignable");
	// Screenshot is also non-movable (deleted constructor)
	SUCCEED();
}
