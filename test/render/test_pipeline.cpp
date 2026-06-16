#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include "render/VulkanContext.h"
#include "render/PipelineBuilder.h"
#include "render/buffers/BufferLayout.h"
#include "render/shaders/ShaderModule.h"
#include "render/shaders/ShaderLib.h"

#include <triangle.vert.h>
#include <triangle.frag.h>

using namespace neurus;

/**
 * @brief Tests for PipelineBuilder — fluent graphics pipeline construction.
 *
 * Creates a headless Vulkan device with dynamicRendering enabled and
 * validates pipeline creation through the builder API.
 *
 * @note Requires a Vulkan-capable GPU. Skipped in headless CI.
 */
class PipelineBuilderTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			m_instance = VulkanContext::CreateInstance();
			m_physicalDevices = vk::raii::PhysicalDevices(m_instance);
			m_hasVulkan = !m_physicalDevices.empty();

			if (m_hasVulkan)
			{
				m_device = std::make_unique<vk::raii::Device>(createHeadlessDevice());
			}
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		// ShaderLib cache must be cleared before device destruction
		ShaderLib::Clear();
	}

	/**
	 * @brief Creates a headless logical device with dynamicRendering enabled.
	 *
	 * Since we build pipelines with VkPipelineRenderingCreateInfo, the
	 * dynamicRendering physical-device feature must be enabled. In Vulkan 1.4
	 * this is core functionality, but still must be explicitly enabled.
	 */
	vk::raii::Device createHeadlessDevice()
	{
		auto& physDevice = m_physicalDevices[0];
		auto queueFamilyProps = physDevice.getQueueFamilyProperties();

		uint32_t graphicsFamily = ~0u;
		for (uint32_t i = 0; i < queueFamilyProps.size(); ++i)
		{
			if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
			{
				graphicsFamily = i;
				break;
			}
		}

		if (graphicsFamily == ~0u)
		{
			throw std::runtime_error(
				"PipelineBuilderTest: no graphics queue family found.");
		}

		float priority = 1.0f;
		vk::DeviceQueueCreateInfo queueCreateInfo({}, graphicsFamily, 1, &priority);

		// Enable dynamicRendering + synchronization2 (required for dynamic rendering)
		vk::PhysicalDeviceDynamicRenderingFeatures dynRendering;
		dynRendering.dynamicRendering = VK_TRUE;
		vk::PhysicalDeviceSynchronization2Features sync2;
		sync2.synchronization2 = VK_TRUE;
		sync2.pNext = &dynRendering;

		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreateInfo);
		deviceCreateInfo.setPNext(&sync2);

		return vk::raii::Device(physDevice, deviceCreateInfo);
	}

	bool m_hasVulkan = false;
	vk::raii::Instance m_instance = nullptr;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;
	std::unique_ptr<vk::raii::Device> m_device;
};

// ---------------------------------------------------------------------------
// Basic pipeline creation
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, BuildGraphicsPipeline_CreatesValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Load shaders from embedded SPIR-V
	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	// Build a simple triangle pipeline
	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()                               // No vertex buffers
		.SetInputAssembly()                             // TriangleList
		.SetRasterization()                             // Fill, no cull
		.SetMultisampling()                             // 1 sample
		.SetDepthStencil(false, false)                  // No depth
		.SetColorBlendAttachment()                      // Standard alpha blend
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})   // One color attachment
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Vertex input from BufferLayout
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, SetVertexInput_FromBufferLayout_ValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	// Build a vertex layout with position + color
	BufferLayout layout;
	layout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);   // float3 pos
	layout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);  // float3 color

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(layout)
		.SetInputAssembly()
		.SetRasterization()
		.SetMultisampling()
		.SetDepthStencil(false, false)
		.SetColorBlendAttachment()
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Depth / stencil
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, SetDepthStencil_WithDepthTest_ValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()
		.SetInputAssembly()
		.SetRasterization()
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLess)
		.SetColorBlendAttachment()
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
		.SetDepthFormat(vk::Format::eD32Sfloat)
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Rasterization overrides
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, SetRasterization_Wireframe_CullBack)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()
		.SetInputAssembly()
		.SetRasterization(
			vk::PolygonMode::eLine,
			vk::CullModeFlagBits::eBack,
			vk::FrontFace::eCounterClockwise)
		.SetMultisampling()
		.SetDepthStencil(false, false)
		.SetColorBlendAttachment()
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Input assembly overrides
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, SetInputAssembly_LineStrip_ValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()
		.SetInputAssembly(vk::PrimitiveTopology::eLineStrip)
		.SetRasterization()
		.SetMultisampling()
		.SetDepthStencil(false, false)
		.SetColorBlendAttachment()
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Descriptor set layouts
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, SetDescriptorSetLayouts_ValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	// Create a simple descriptor set layout with one UBO binding
	vk::DescriptorSetLayoutBinding uboBinding(
		0,
		vk::DescriptorType::eUniformBuffer,
		1,
		vk::ShaderStageFlagBits::eVertex);

	vk::DescriptorSetLayoutCreateInfo layoutCreateInfo({}, uboBinding);
	vk::raii::DescriptorSetLayout descriptorSetLayout(*m_device, layoutCreateInfo);

	std::vector<vk::DescriptorSetLayout> layouts = { *descriptorSetLayout };

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()
		.SetInputAssembly()
		.SetRasterization()
		.SetMultisampling()
		.SetDepthStencil(false, false)
		.SetColorBlendAttachment()
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
		.SetDescriptorSetLayouts(layouts)
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Push constant ranges
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, SetPushConstantRanges_ValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eVertex,
		0,
		sizeof(float) * 16);  // Typical 4x4 matrix

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()
		.SetInputAssembly()
		.SetRasterization()
		.SetMultisampling()
		.SetDepthStencil(false, false)
		.SetColorBlendAttachment()
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
		.SetPushConstantRanges({pushRange})
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Pipeline cache
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, SetPipelineCache_CreatesValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	vk::raii::PipelineCache cache(*m_device, vk::PipelineCacheCreateInfo());

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()
		.SetInputAssembly()
		.SetRasterization()
		.SetMultisampling()
		.SetDepthStencil(false, false)
		.SetColorBlendAttachment()
		.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
		.SetPipelineCache(&cache)
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Missing shader stages — should throw
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, BuildGraphicsPipeline_NoStages_Throws)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	EXPECT_THROW(
		{
			PipelineBuilder()
				.SetVertexInput()
				.SetInputAssembly()
				.SetRasterization()
				.SetMultisampling()
				.SetDepthStencil(false, false)
				.SetColorBlendAttachment()
				.SetColorFormats({vk::Format::eB8G8R8A8Srgb})
				.BuildGraphicsPipeline(*m_device);
		},
		std::runtime_error);
}

// ---------------------------------------------------------------------------
// Missing color formats — should throw
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, BuildGraphicsPipeline_NoColorFormats_Throws)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	EXPECT_THROW(
		{
			PipelineBuilder()
				.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
				.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
				.SetVertexInput()
				.SetInputAssembly()
				.SetRasterization()
				.SetMultisampling()
				.SetDepthStencil(false, false)
				.SetColorBlendAttachment()
				// No SetColorFormats() call — should throw
				.BuildGraphicsPipeline(*m_device);
		},
		std::runtime_error);
}

// ---------------------------------------------------------------------------
// Depth-only pipeline (no color attachments)
// ---------------------------------------------------------------------------

TEST_F(PipelineBuilderTest, DepthOnly_NoColorAttachments_ValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto vertModule = ShaderModule::FromEmbedded(
		*m_device, triangle_vert_spv, triangle_vert_spv_size);
	auto fragModule = ShaderModule::FromEmbedded(
		*m_device, triangle_frag_spv, triangle_frag_spv_size);

	auto pipeline = PipelineBuilder()
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput()
		.SetInputAssembly()
		.SetRasterization()
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLess)
		.ClearColorBlendAttachments()                // No color attachment
		.SetDepthFormat(vk::Format::eD32Sfloat)       // Depth only
		.BuildGraphicsPipeline(*m_device);

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}
