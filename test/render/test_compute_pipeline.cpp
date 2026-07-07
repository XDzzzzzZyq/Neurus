#include <gtest/gtest.h>

#include "render/ComputePipelineBuilder.h"
#include "render/shaders/ShaderModule.h"
#include "render/DescriptorManager.h"
#include "app/VulkanContext.h"

using namespace neurus;

// Minimal valid compute SPIR-V generated via glslangValidator
static const uint32_t kMinimalCompSpv[] = {
	0x07230203, 0x00010000, 0x0008000B, 0x0000000A,
	0x00000000, 0x00020011, 0x00000001, 0x0006000B,
	0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E,
	0x00000000, 0x0003000E, 0x00000000, 0x00000001,
	0x0005000F, 0x00000005, 0x00000004, 0x6E69616D,
	0x00000000, 0x00060010, 0x00000004, 0x00000011,
	0x00000001, 0x00000001, 0x00000001, 0x00030003,
	0x00000002, 0x000001C2, 0x00040005, 0x00000004,
	0x6E69616D, 0x00000000, 0x00040047, 0x00000009,
	0x0000000B, 0x00000019, 0x00020013, 0x00000002,
	0x00030021, 0x00000003, 0x00000002, 0x00040015,
	0x00000006, 0x00000020, 0x00000000, 0x00040017,
	0x00000007, 0x00000006, 0x00000003, 0x0004002B,
	0x00000006, 0x00000008, 0x00000001, 0x0006002C,
	0x00000007, 0x00000009, 0x00000008, 0x00000008,
	0x00000008, 0x00050036, 0x00000002, 0x00000004,
	0x00000000, 0x00000003, 0x000200F8, 0x00000005,
	0x000100FD, 0x00010038,
};
static const size_t kMinimalCompSpvSize = sizeof(kMinimalCompSpv);

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
	vk::raii::Pipeline pipeline = ComputePipelineBuilder(*m_device)
		.SetShaderStage(shader)
		.BuildComputePipeline();

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
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

	vk::raii::Pipeline pipeline = ComputePipelineBuilder(*m_device)
		.SetShaderStage(shader)
		.AddDescriptorSetLayout(*layout.layout())
		.BuildComputePipeline();

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
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

	vk::raii::Pipeline pipeline = ComputePipelineBuilder(*m_device)
		.SetShaderStage(shader)
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
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

	vk::raii::Pipeline pipeline = ComputePipelineBuilder(*m_device)
		.SetShaderStage(shader)
		.AddDescriptorSetLayout(*layout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();

	EXPECT_NE(*pipeline, VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------
// ComputePipelineBuilder is reusable - two successive builds
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

	vk::raii::Pipeline pipelineA = ComputePipelineBuilder(*m_device)
		.SetShaderStage(shaderA)
		.BuildComputePipeline();

	EXPECT_NE(*pipelineA, VK_NULL_HANDLE);

	vk::raii::Pipeline pipelineB = ComputePipelineBuilder(*m_device)
		.SetShaderStage(shaderB)
		.BuildComputePipeline();

	EXPECT_NE(*pipelineB, VK_NULL_HANDLE);

	// Two different pipelines should have different handles
	EXPECT_NE(*pipelineA, *pipelineB);
}
