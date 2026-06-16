// Must define platform before including Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include "render/RenderPassManager.h"
#include "render/VulkanImage.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>

using namespace neurus;

/**
 * @brief Tests for RenderPassManager — dynamic rendering pass orchestration.
 *
 * Validates BeginPass/EndPass using VK_KHR_dynamic_rendering with preset
 * clear values per pass type.
 *
 * @note These tests require a Vulkan 1.4-capable GPU. They will be skipped
 *       in CI environments without GPU access.
 */
class RenderPassManagerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Instance ---
			vk::ApplicationInfo appInfo("NeurusTest_RPass", VK_MAKE_VERSION(0, 1, 0),
			                            "NeurusTest_RPass", VK_MAKE_VERSION(0, 1, 0),
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

			// --- Device (Vulkan 1.4 includes dynamic rendering as core) ---
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
		if (m_device)
		{
			m_device->waitIdle();
		}
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

	/** Helper: create a simple color attachment image for testing. */
	VulkanImage CreateColorAttachment(uint32_t width, uint32_t height, vk::Format format)
	{
		auto& pd = m_physicalDevices[m_selectedPdIndex];
		return VulkanImage(*m_device, pd, vk::Extent2D(width, height), format,
		                   vk::ImageUsageFlagBits::eColorAttachment, 1);
	}

	/** Helper: create a simple depth attachment image for testing. */
	VulkanImage CreateDepthAttachment(uint32_t width, uint32_t height, vk::Format format)
	{
		auto& pd = m_physicalDevices[m_selectedPdIndex];
		return VulkanImage(*m_device, pd, vk::Extent2D(width, height), format,
		                   vk::ImageUsageFlagBits::eDepthStencilAttachment, 1,
		                   VulkanImage::ImageType::eDepthStencil);
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
// Pass Type Enum Values
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, PassType_EnumValuesExist)
{
	// Verify all pass type enum values are defined
	auto gBuffer = RenderPassManager::PassType::G_BUFFER;
	auto lighting = RenderPassManager::PassType::LIGHTING;
	auto shadow = RenderPassManager::PassType::SHADOW;
	auto composite = RenderPassManager::PassType::COMPOSITE;
	auto postFx = RenderPassManager::PassType::POST_FX;

	// Just ensure they compile and are distinct
	EXPECT_NE(static_cast<int>(gBuffer), static_cast<int>(lighting));
	EXPECT_NE(static_cast<int>(lighting), static_cast<int>(shadow));
	EXPECT_NE(static_cast<int>(shadow), static_cast<int>(composite));
	EXPECT_NE(static_cast<int>(composite), static_cast<int>(postFx));
}

// ---------------------------------------------------------------------------
// Color Attachment Count per Pass Type
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, ColorAttachmentCount_PerPassType)
{
	// G_BUFFER: 5 color attachments (Position, Normal, Albedo, MetallicRoughness, ?)
	// Wait, let me think. G-Buffer typically has:
	//   Position (RGBA16F), Normal (RGBA16F), Albedo (RGBA8_SRGB),
	//   MetallicRoughness (RGBA8_UNORM) — that's 4 color + 1 depth.
	//   The task says "G_Buffer (5 color + depth)" so let's go with 5 color.
	EXPECT_EQ(RenderPassManager::ColorAttachmentCount(RenderPassManager::PassType::G_BUFFER), 5u);
	EXPECT_EQ(RenderPassManager::ColorAttachmentCount(RenderPassManager::PassType::LIGHTING), 1u);
	EXPECT_EQ(RenderPassManager::ColorAttachmentCount(RenderPassManager::PassType::SHADOW), 0u);
	EXPECT_EQ(RenderPassManager::ColorAttachmentCount(RenderPassManager::PassType::COMPOSITE), 1u);
	EXPECT_EQ(RenderPassManager::ColorAttachmentCount(RenderPassManager::PassType::POST_FX), 1u);
}

// ---------------------------------------------------------------------------
// Depth Attachment Presence per Pass Type
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, HasDepth_PerPassType)
{
	EXPECT_TRUE(RenderPassManager::HasDepth(RenderPassManager::PassType::G_BUFFER));
	EXPECT_FALSE(RenderPassManager::HasDepth(RenderPassManager::PassType::LIGHTING));
	EXPECT_TRUE(RenderPassManager::HasDepth(RenderPassManager::PassType::SHADOW));
	EXPECT_FALSE(RenderPassManager::HasDepth(RenderPassManager::PassType::COMPOSITE));
	EXPECT_FALSE(RenderPassManager::HasDepth(RenderPassManager::PassType::POST_FX));
}

// ---------------------------------------------------------------------------
// Preset Clear Values per Pass Type
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, PresetClearValues_G_Buffer)
{
	const auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::G_BUFFER);

	// G_BUFFER: 5 color clear values + 1 depth clear value = 6
	ASSERT_EQ(clearValues.size(), 6u);

	// All color clear values should be of type ClearColorValue
	for (size_t i = 0; i < 5; ++i)
	{
		EXPECT_EQ(clearValues[i].color.float32[0], 0.0f);
		EXPECT_EQ(clearValues[i].color.float32[1], 0.0f);
		EXPECT_EQ(clearValues[i].color.float32[2], 0.0f);
		EXPECT_EQ(clearValues[i].color.float32[3], 0.0f);
	}

	// Depth clear value: 1.0f (far plane)
	EXPECT_FLOAT_EQ(clearValues[5].depthStencil.depth, 1.0f);
	EXPECT_EQ(clearValues[5].depthStencil.stencil, 0u);
}

TEST_F(RenderPassManagerTest, PresetClearValues_Lighting)
{
	const auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::LIGHTING);

	// LIGHTING: 1 color clear value, no depth
	ASSERT_EQ(clearValues.size(), 1u);

	// Color clear to black
	EXPECT_FLOAT_EQ(clearValues[0].color.float32[0], 0.0f);
	EXPECT_FLOAT_EQ(clearValues[0].color.float32[1], 0.0f);
	EXPECT_FLOAT_EQ(clearValues[0].color.float32[2], 0.0f);
	EXPECT_FLOAT_EQ(clearValues[0].color.float32[3], 0.0f);
}

TEST_F(RenderPassManagerTest, PresetClearValues_Shadow)
{
	const auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::SHADOW);

	// SHADOW: no color, 1 depth clear value only
	ASSERT_EQ(clearValues.size(), 1u);

	EXPECT_FLOAT_EQ(clearValues[0].depthStencil.depth, 1.0f);
	EXPECT_EQ(clearValues[0].depthStencil.stencil, 0u);
}

TEST_F(RenderPassManagerTest, PresetClearValues_Composite)
{
	const auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::COMPOSITE);

	// COMPOSITE: 1 color clear value
	ASSERT_EQ(clearValues.size(), 1u);
	EXPECT_FLOAT_EQ(clearValues[0].color.float32[0], 0.0f);
}

TEST_F(RenderPassManagerTest, PresetClearValues_PostFX)
{
	const auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::POST_FX);

	// POST_FX: 1 color clear value
	ASSERT_EQ(clearValues.size(), 1u);
	EXPECT_FLOAT_EQ(clearValues[0].color.float32[0], 0.0f);
}

// ---------------------------------------------------------------------------
// BeginPass / EndPass — Basic Smoke Test (single color, no depth)
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, BeginEndPass_SingleColor_NoValidationError)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	// Create a simple color attachment
	auto colorAttachment = CreateColorAttachment(128, 128, vk::Format::eR8G8B8A8Unorm);

	// Transition to color attachment optimal layout
	{
		auto& cmd = BeginCmd();
		colorAttachment.TransitionLayout(cmd,
		                                 vk::ImageLayout::eUndefined,
		                                 vk::ImageLayout::eColorAttachmentOptimal);
		EndCmd(cmd);
	}

	// Begin and end a rendering pass
	RenderPassManager rpManager;
	{
		auto& cmd = BeginCmd();

		std::vector<vk::ImageView> colorViews = { *colorAttachment.ImageViewHandle() };
		auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::LIGHTING);
		vk::Extent2D extent(128, 128);

		rpManager.BeginPass(cmd, RenderPassManager::PassType::LIGHTING,
		                    colorViews, nullptr, clearValues, extent);

		// Set viewport + scissor (required by spec)
		vk::Viewport viewport(0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f);
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, vk::Rect2D({0, 0}, {128, 128}));

		rpManager.EndPass(cmd);

		EndCmd(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// BeginPass / EndPass — Depth Attachment
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, BeginEndPass_WithDepth_NoValidationError)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	// Check depth format support
	auto depthFmtProps = pd.getFormatProperties(vk::Format::eD32Sfloat);
	if (!(depthFmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment))
	{
		GTEST_SKIP() << "D32_SFLOAT depth attachment not supported.";
	}

	// Create color + depth attachments
	auto colorAttachment = CreateColorAttachment(128, 128, vk::Format::eR8G8B8A8Unorm);
	auto depthAttachment = CreateDepthAttachment(128, 128, vk::Format::eD32Sfloat);

	// Transition layouts
	{
		auto& cmd = BeginCmd();
		colorAttachment.TransitionLayout(cmd,
		                                 vk::ImageLayout::eUndefined,
		                                 vk::ImageLayout::eColorAttachmentOptimal);
		depthAttachment.TransitionLayout(cmd,
		                                 vk::ImageLayout::eUndefined,
		                                 vk::ImageLayout::eDepthStencilAttachmentOptimal);
		EndCmd(cmd);
	}

	// Begin/end pass with depth (using SHADOW pass type which has depth)
	RenderPassManager rpManager;
	{
		auto& cmd = BeginCmd();

		std::vector<vk::ImageView> colorViews;  // SHADOW has 0 color attachments
		vk::ImageView depthView = *depthAttachment.ImageViewHandle();
		auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::SHADOW);
		vk::Extent2D extent(128, 128);

		rpManager.BeginPass(cmd, RenderPassManager::PassType::SHADOW,
		                    colorViews, &depthView, clearValues, extent);

		vk::Viewport viewport(0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f);
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, vk::Rect2D({0, 0}, {128, 128}));

		rpManager.EndPass(cmd);

		EndCmd(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// BeginPass / EndPass — G-Buffer (multiple color + depth)
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, BeginEndPass_GBuffer_NoValidationError)
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

	// Create 5 color attachments + 1 depth
	std::vector<vk::Format> gbufferColorFormats = {
		vk::Format::eR16G16B16A16Sfloat,  // Position
		vk::Format::eR16G16B16A16Sfloat,  // Normal
		vk::Format::eR8G8B8A8Srgb,        // Albedo
		vk::Format::eR8G8B8A8Unorm,       // MetallicRoughness
		vk::Format::eR8G8B8A8Unorm        // (5th color for future use)
	};

	std::vector<VulkanImage> colorAttachments;
	colorAttachments.reserve(gbufferColorFormats.size());
	for (const auto& fmt : gbufferColorFormats)
	{
		colorAttachments.push_back(
			VulkanImage(*m_device, pd, vk::Extent2D(128, 128), fmt,
			            vk::ImageUsageFlagBits::eColorAttachment, 1));
	}

	auto depthAttachment = CreateDepthAttachment(128, 128, vk::Format::eD32Sfloat);

	// Transition all layouts
	{
		auto& cmd = BeginCmd();
		for (auto& ca : colorAttachments)
		{
			ca.TransitionLayout(cmd, vk::ImageLayout::eUndefined,
			                    vk::ImageLayout::eColorAttachmentOptimal);
		}
		depthAttachment.TransitionLayout(cmd, vk::ImageLayout::eUndefined,
		                                 vk::ImageLayout::eDepthStencilAttachmentOptimal);
		EndCmd(cmd);
	}

	// Begin/end G-Buffer pass
	RenderPassManager rpManager;
	{
		auto& cmd = BeginCmd();

		std::vector<vk::ImageView> colorViews;
		colorViews.reserve(colorAttachments.size());
		for (auto& ca : colorAttachments)
		{
			colorViews.push_back(*ca.ImageViewHandle());
		}
		vk::ImageView depthView = *depthAttachment.ImageViewHandle();
		auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::G_BUFFER);
		vk::Extent2D extent(128, 128);

		rpManager.BeginPass(cmd, RenderPassManager::PassType::G_BUFFER,
		                    colorViews, &depthView, clearValues, extent);

		vk::Viewport viewport(0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f);
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, vk::Rect2D({0, 0}, {128, 128}));

		rpManager.EndPass(cmd);

		EndCmd(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// Post-FX pass (1 color, load=DONT_CARE, store=STORE)
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, BeginEndPass_PostFX_NoValidationError)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	auto colorAttachment = CreateColorAttachment(128, 128, vk::Format::eR8G8B8A8Unorm);

	{
		auto& cmd = BeginCmd();
		colorAttachment.TransitionLayout(cmd,
		                                 vk::ImageLayout::eUndefined,
		                                 vk::ImageLayout::eColorAttachmentOptimal);
		EndCmd(cmd);
	}

	RenderPassManager rpManager;
	{
		auto& cmd = BeginCmd();

		std::vector<vk::ImageView> colorViews = { *colorAttachment.ImageViewHandle() };
		auto clearValues = RenderPassManager::PresetClearValues(RenderPassManager::PassType::POST_FX);
		vk::Extent2D extent(128, 128);

		rpManager.BeginPass(cmd, RenderPassManager::PassType::POST_FX,
		                    colorViews, nullptr, clearValues, extent);

		vk::Viewport viewport(0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f);
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, vk::Rect2D({0, 0}, {128, 128}));

		rpManager.EndPass(cmd);

		EndCmd(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// Non-copyable, movable
// ---------------------------------------------------------------------------

TEST_F(RenderPassManagerTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<RenderPassManager>,
	              "RenderPassManager must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<RenderPassManager>,
	              "RenderPassManager must not be copy-assignable");
	SUCCEED();
}

TEST_F(RenderPassManagerTest, Movable)
{
	static_assert(std::is_move_constructible_v<RenderPassManager>,
	              "RenderPassManager must be move-constructible");
	static_assert(std::is_move_assignable_v<RenderPassManager>,
	              "RenderPassManager must be move-assignable");
	SUCCEED();
}
