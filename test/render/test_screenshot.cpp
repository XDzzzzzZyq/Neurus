#include <gtest/gtest.h>

#include "TestVulkanFixture.h"
#include "render/Screenshot.h"
#include "render/VulkanImage.h"

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
 * Creates a VulkanImage, uploads known pixel data, captures to PNG,
 * and verifies file existence and non-zero size.
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class ScreenshotTest : public VulkanTestFixture
{
protected:
	void SetUp() override
	{
		VulkanTestFixture::SetUp();
	}

	void TearDown() override
	{
		// Clean up test output file
		if (!m_testOutputPath.empty() && std::filesystem::exists(m_testOutputPath))
		{
			std::filesystem::remove(m_testOutputPath);
		}
		VulkanTestFixture::TearDown();
	}

	/** Shortcut: record commands inside a one-shot buffer. */
	template <typename F>
	void OneShotSubmit(F&& recordFn)
	{
		auto& cmd = BeginCmd();
		recordFn(cmd);
		EndSubmitWait(cmd);
	}

	/**
	 * @brief Uploads pixel data into a VulkanImage via staging buffer.
	 *
	 * Creates a staging buffer, copies data via vkCmdCopyBufferToImage,
	 * then transitions the image to TRANSFER_SRC_OPTIMAL for readback.
	 */
	void uploadImageData(vk::Image image, vk::Extent2D extent, vk::Format format,
	                     const void* data, size_t dataSize)
	{
		auto& pd = m_physicalDevices[m_selectedPdIndex];

		// --- Create staging buffer ---
		vk::BufferCreateInfo stagingCI({}, dataSize, vk::BufferUsageFlagBits::eTransferSrc);
		vk::raii::Buffer stagingBuffer(*m_device, stagingCI);

		auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
		uint32_t stagingMemType = findMemoryType(pd, stagingMemReqs.memoryTypeBits,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		vk::MemoryAllocateInfo stagingAlloc(stagingMemReqs.size, stagingMemType);
		vk::raii::DeviceMemory stagingMemory(*m_device, stagingAlloc);
		stagingBuffer.bindMemory(*stagingMemory, 0);

		void* mapped = stagingMemory.mapMemory(0, dataSize);
		std::memcpy(mapped, data, dataSize);
		stagingMemory.unmapMemory();

		OneShotSubmit([&](vk::raii::CommandBuffer& cmd) {
			// Transition image to TRANSFER_DST
			vk::ImageMemoryBarrier barrier1(
				vk::AccessFlagBits::eNone,
				vk::AccessFlagBits::eTransferWrite,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eTransferDstOptimal,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				image,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
			                    vk::PipelineStageFlagBits::eTransfer,
			                    {}, {}, {}, barrier1);

			// Copy buffer to image
			vk::BufferImageCopy copyRegion(0, 0, 0,
				vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
				vk::Offset3D(0, 0, 0),
				vk::Extent3D(extent.width, extent.height, 1));
			cmd.copyBufferToImage(*stagingBuffer, image,
			                      vk::ImageLayout::eTransferDstOptimal, copyRegion);

			// Transition to SHADER_READ_ONLY (simulating post-render state)
			vk::ImageMemoryBarrier barrier2(
				vk::AccessFlagBits::eTransferWrite,
				vk::AccessFlagBits::eShaderRead,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				image,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			                    vk::PipelineStageFlagBits::eFragmentShader,
			                    {}, {}, {}, barrier2);
		});
	}

	static uint32_t findMemoryType(const vk::raii::PhysicalDevice& pd,
	                               uint32_t typeBits,
	                               vk::MemoryPropertyFlags required)
	{
		auto memProps = pd.getMemoryProperties();
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & required) == required)
			{
				return i;
			}
		}
		throw std::runtime_error("No suitable memory type found");
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

	// --- Create a VulkanImage with TRANSFER_SRC | TRANSFER_DST | SAMPLED ---
	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  1);

	// --- Upload red pixel data ---
	const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
	std::vector<uint8_t> redPixels(pixelCount * 4);
	for (size_t i = 0; i < pixelCount; ++i)
	{
		redPixels[i * 4 + 0] = 255;  // R
		redPixels[i * 4 + 1] = 0;    // G
		redPixels[i * 4 + 2] = 0;    // B
		redPixels[i * 4 + 3] = 255;  // A
	}
	uploadImageData(*image.ImageHandle(), extent, vk::Format::eR8G8B8A8Unorm,
	                redPixels.data(), redPixels.size());

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

	// --- Create a VulkanImage with RGBA16F format ---
	VulkanImage image(*m_device, pd, extent, vk::Format::eR16G16B16A16Sfloat,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  1);

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
	uploadImageData(*image.ImageHandle(), extent, vk::Format::eR16G16B16A16Sfloat,
	                halfData.data(), halfData.size() * sizeof(uint16_t));

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

	VulkanImage image(*m_device, pd, extent, vk::Format::eR8G8B8A8Unorm,
	                  vk::ImageUsageFlagBits::eSampled |
	                      vk::ImageUsageFlagBits::eTransferSrc |
	                      vk::ImageUsageFlagBits::eTransferDst,
	                  1);

	const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
	std::vector<uint8_t> pixels(pixelCount * 4, 128);
	uploadImageData(*image.ImageHandle(), extent, vk::Format::eR8G8B8A8Unorm,
	                pixels.data(), pixels.size());

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
