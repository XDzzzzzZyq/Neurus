#include <gtest/gtest.h>

#include "render/PipelineBuilder.h"
#include "render/shaders/ShaderModule.h"
#include "render/DescriptorManager.h"
#include "app/VulkanContext.h"
#include "platform/PlatformSurface.h"
#include "shared/TestMinimalSpv.h"

using namespace neurus;

/**
 * @brief Tests for ComputePipelineBuilder.
 *
 * These tests require a Vulkan-capable GPU with compute support.
 * In CI environments without GPU, they are skipped.
 */
class ComputePipelineTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			auto platform = CreatePlatformSurface(); m_instance = VulkanContext::CreateInstance(*platform);
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

	/**
	 * @brief Creates a logical device without requiring a surface.
	 *
	 * Picks the first physical device with a graphics-capable queue family
	 * (which implicitly supports compute per the Vulkan spec).
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
			throw std::runtime_error("No graphics queue family found on physical device");
		}

		float priority = 1.0f;
		vk::DeviceQueueCreateInfo queueCreateInfo({}, graphicsFamily, 1, &priority);
		vk::DeviceCreateInfo deviceCreateInfo({}, queueCreateInfo);

		return vk::raii::Device(physDevice, deviceCreateInfo);
	}

	bool m_hasVulkan = false;
	vk::raii::Instance m_instance = nullptr;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;
	std::unique_ptr<vk::raii::Device> m_device;
};

// ---------------------------------------------------------------------------
// Basic compute pipeline creation
// ---------------------------------------------------------------------------

TEST_F(ComputePipelineTest, Build_ReturnsValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// Create compute shader module from inline SPIR-V
	std::vector<uint32_t> spirv(
		kMinimalCompSpv,
		kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));
	ShaderModule shader(*m_device, spirv);
	ASSERT_NE(*shader.handle(), VK_NULL_HANDLE);

	// Build the compute pipeline
	Pipeline pipeline = PipelineBuilder()
		.AddShaderStage(shader.GetStageInfo(vk::ShaderStageFlagBits::eCompute))
		.BuildComputePipeline(*m_device);

	EXPECT_NE(*pipeline.pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Compute pipeline with descriptor set layout
// ---------------------------------------------------------------------------

TEST_F(ComputePipelineTest, WithDescriptorSetLayout_BuildsSuccessfully)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<uint32_t> spirv(
		kMinimalCompSpv,
		kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));
	ShaderModule shader(*m_device, spirv);
	ASSERT_NE(*shader.handle(), VK_NULL_HANDLE);

	// Create a descriptor set layout with a storage buffer binding
	auto layout = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eStorageBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(*m_device);

	Pipeline pipeline = PipelineBuilder()
		.AddShaderStage(shader.GetStageInfo(vk::ShaderStageFlagBits::eCompute))
		.AddDescriptorSetLayout(*layout.layout())
		.BuildComputePipeline(*m_device);

	EXPECT_NE(*pipeline.pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Compute pipeline with push constant range
// ---------------------------------------------------------------------------

TEST_F(ComputePipelineTest, WithPushConstants_BuildsSuccessfully)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<uint32_t> spirv(
		kMinimalCompSpv,
		kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));
	ShaderModule shader(*m_device, spirv);
	ASSERT_NE(*shader.handle(), VK_NULL_HANDLE);

	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute, 0, 16);

	Pipeline pipeline = PipelineBuilder()
		.AddShaderStage(shader.GetStageInfo(vk::ShaderStageFlagBits::eCompute))
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline(*m_device);

	EXPECT_NE(*pipeline.pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// Compute pipeline with both descriptor layout and push constants
// ---------------------------------------------------------------------------

TEST_F(ComputePipelineTest, WithLayoutAndPushConstants_BuildsSuccessfully)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<uint32_t> spirv(
		kMinimalCompSpv,
		kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));
	ShaderModule shader(*m_device, spirv);
	ASSERT_NE(*shader.handle(), VK_NULL_HANDLE);

	auto layout = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(*m_device);

	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute, 0, 32);

	Pipeline pipeline = PipelineBuilder()
		.AddShaderStage(shader.GetStageInfo(vk::ShaderStageFlagBits::eCompute))
		.AddDescriptorSetLayout(*layout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline(*m_device);

	EXPECT_NE(*pipeline.pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// PipelineBuilder is reusable - two successive builds
// ---------------------------------------------------------------------------

TEST_F(ComputePipelineTest, TwoBuilds_BothProduceValidPipelines)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	std::vector<uint32_t> spirv(
		kMinimalCompSpv,
		kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));
	ShaderModule shaderA(*m_device, spirv);
	ASSERT_NE(*shaderA.handle(), VK_NULL_HANDLE);

	ShaderModule shaderB(*m_device, spirv);
	ASSERT_NE(*shaderB.handle(), VK_NULL_HANDLE);

	Pipeline pipelineA = PipelineBuilder()
		.AddShaderStage(shaderA.GetStageInfo(vk::ShaderStageFlagBits::eCompute))
		.BuildComputePipeline(*m_device);

	EXPECT_NE(*pipelineA.pipeline, VK_NULL_HANDLE);

	Pipeline pipelineB = PipelineBuilder()
		.AddShaderStage(shaderB.GetStageInfo(vk::ShaderStageFlagBits::eCompute))
		.BuildComputePipeline(*m_device);

	EXPECT_NE(*pipelineB.pipeline, VK_NULL_HANDLE);

	// Two different pipelines should have different handles
	EXPECT_NE(*pipelineA.pipeline, *pipelineB.pipeline);
}
