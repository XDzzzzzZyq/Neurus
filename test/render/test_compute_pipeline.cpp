#include <gtest/gtest.h>

#include "render/ComputePipelineBuilder.h"
#include "render/shaders/ShaderModule.h"
#include "render/DescriptorManager.h"
#include "render/VulkanContext.h"

#include <dummy.comp.h>

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

	// Create a compute shader module from embedded SPIR-V
	ShaderModule shader(*m_device,
		std::vector<uint32_t>(
			dummy_comp_spv,
			dummy_comp_spv + (dummy_comp_spv_size / sizeof(uint32_t))));

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

	ShaderModule shader(*m_device,
		std::vector<uint32_t>(
			dummy_comp_spv,
			dummy_comp_spv + (dummy_comp_spv_size / sizeof(uint32_t))));

	// Create a descriptor set layout with a storage buffer binding
	auto bindings = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eStorageBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		.Build();

	DescriptorSetLayout layout(*m_device, bindings);

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

	ShaderModule shader(*m_device,
		std::vector<uint32_t>(
			dummy_comp_spv,
			dummy_comp_spv + (dummy_comp_spv_size / sizeof(uint32_t))));

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

	ShaderModule shader(*m_device,
		std::vector<uint32_t>(
			dummy_comp_spv,
			dummy_comp_spv + (dummy_comp_spv_size / sizeof(uint32_t))));

	auto bindings = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		.Build();

	DescriptorSetLayout layout(*m_device, bindings);

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
// ComputePipelineBuilder is reusable — two successive builds
// ---------------------------------------------------------------------------

TEST_F(ComputePipelineTest, TwoBuilds_BothProduceValidPipelines)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ShaderModule shaderA(*m_device,
		std::vector<uint32_t>(
			dummy_comp_spv,
			dummy_comp_spv + (dummy_comp_spv_size / sizeof(uint32_t))));

	ShaderModule shaderB(*m_device,
		std::vector<uint32_t>(
			dummy_comp_spv,
			dummy_comp_spv + (dummy_comp_spv_size / sizeof(uint32_t))));

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
