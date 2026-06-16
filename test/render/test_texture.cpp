// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "render/Texture.h"
#include "render/VulkanContext.h"

using namespace neurus;

/**
 * @brief Tests for Texture - RAII texture wrapping VulkanImage + Sampler.
 *
 * Creates Vulkan instance and device without a surface (graphics queue
 * without present support is sufficient for texture operations).
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class TextureTest : public ::testing::Test
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
// FromData - embedded pixel data → GPU texture
// ---------------------------------------------------------------------------

TEST_F(TextureTest, FromData_4x4_RGBA8_Valid)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create a simple 4×4 solid-red image (RGBA8)
	constexpr uint32_t kWidth = 4;
	constexpr uint32_t kHeight = 4;
	constexpr size_t kDataSize = kWidth * kHeight * 4;
	std::array<uint8_t, kDataSize> pixels;
	for (size_t i = 0; i < kDataSize; i += 4)
	{
		pixels[i + 0] = 255; // R
		pixels[i + 1] = 0;   // G
		pixels[i + 2] = 0;   // B
		pixels[i + 3] = 255; // A
	}

	Texture texture;
	ASSERT_NO_THROW({
		texture = Texture::FromData(
			*m_device,
			(*m_physicalDevices)[0],
			m_queue,
			m_queueFamilyIndex,
			kWidth,
			kHeight,
			pixels.data(),
			vk::Format::eR8G8B8A8Srgb);
	});

	EXPECT_TRUE(texture.IsValid());
	EXPECT_EQ(texture.Extent().width, kWidth);
	EXPECT_EQ(texture.Extent().height, kHeight);
	EXPECT_EQ(texture.Format(), vk::Format::eR8G8B8A8Srgb);
	// With mipmaps, mipLevels should be > 1 for 4x4
	EXPECT_GT(texture.MipLevels(), 1u);
	// Sampler should be valid
	EXPECT_TRUE(texture.HasSampler());
}

TEST_F(TextureTest, FromData_2x2_HDR_Valid)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create a simple 2×2 HDR image (RGBA32F)
	constexpr uint32_t kWidth = 2;
	constexpr uint32_t kHeight = 2;
	constexpr size_t kFloats = kWidth * kHeight * 4;
	std::array<float, kFloats> pixels;
	for (size_t i = 0; i < kFloats; i += 4)
	{
		pixels[i + 0] = 1.0f;  // R
		pixels[i + 1] = 0.5f;  // G
		pixels[i + 2] = 0.25f; // B
		pixels[i + 3] = 1.0f;  // A
	}

	Texture texture;
	ASSERT_NO_THROW({
		texture = Texture::FromData(
			*m_device,
			(*m_physicalDevices)[0],
			m_queue,
			m_queueFamilyIndex,
			kWidth,
			kHeight,
			pixels.data(),
			vk::Format::eR32G32B32A32Sfloat);
	});

	EXPECT_TRUE(texture.IsValid());
	EXPECT_EQ(texture.Extent().width, kWidth);
	EXPECT_EQ(texture.Extent().height, kHeight);
	EXPECT_EQ(texture.Format(), vk::Format::eR32G32B32A32Sfloat);
	EXPECT_TRUE(texture.HasSampler());
}

// ---------------------------------------------------------------------------
// ForAttachment - framebuffer attachment (no sampler)
// ---------------------------------------------------------------------------

TEST_F(TextureTest, ForAttachment_64x64_Color)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr uint32_t kSize = 64;
	Texture texture;
	ASSERT_NO_THROW({
		texture = Texture::ForAttachment(
			*m_device,
			(*m_physicalDevices)[0],
			vk::Extent2D{kSize, kSize},
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
	});

	EXPECT_TRUE(texture.IsValid());
	EXPECT_EQ(texture.Extent().width, kSize);
	EXPECT_EQ(texture.Extent().height, kSize);
	EXPECT_EQ(texture.Format(), vk::Format::eR8G8B8A8Unorm);
	EXPECT_EQ(texture.MipLevels(), 1u);
	// ForAttachment with sampled usage still creates a sampler
	EXPECT_TRUE(texture.HasSampler());
}

// ---------------------------------------------------------------------------
// SamplerConfig customisation
// ---------------------------------------------------------------------------

TEST_F(TextureTest, FromData_CustomSampler)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	constexpr uint32_t kSize = 2;
	constexpr size_t kDataSize = kSize * kSize * 4;
	std::array<uint8_t, kDataSize> pixels{};

	SamplerConfig config;
	config.magFilter = vk::Filter::eNearest;
	config.minFilter = vk::Filter::eNearest;
	config.addressModeU = vk::SamplerAddressMode::eClampToEdge;
	config.addressModeV = vk::SamplerAddressMode::eClampToEdge;

	Texture texture;
	ASSERT_NO_THROW({
		texture = Texture::FromData(
			*m_device,
			(*m_physicalDevices)[0],
			m_queue,
			m_queueFamilyIndex,
			kSize,
			kSize,
			pixels.data(),
			vk::Format::eR8G8B8A8Srgb,
			config);
	});

	EXPECT_TRUE(texture.IsValid());
	EXPECT_TRUE(texture.HasSampler());
	EXPECT_EQ(texture.MipLevels(), 2u); // 2x2 → 2 mip levels
}

// ---------------------------------------------------------------------------
// Default-constructed Texture is invalid
// ---------------------------------------------------------------------------

TEST_F(TextureTest, DefaultConstructed_IsInvalid)
{
	// No Vulkan needed for this test
	Texture texture;
	EXPECT_FALSE(texture.IsValid());
	EXPECT_FALSE(texture.HasSampler());
}
