#include "ComputePipelineBuilder.h"
#include "shaders/ShaderModule.h"

#include <stdexcept>

namespace neurus {

ComputePipelineBuilder::ComputePipelineBuilder(const vk::raii::Device& device)
	: m_device(device)
{
}

ComputePipelineBuilder& ComputePipelineBuilder::SetShaderStage(
	const ShaderModule& shader,
	const char* entryPoint)
{
	// Build the shader stage create info
	m_stageInfo = vk::PipelineShaderStageCreateInfo(
		{},                                          // flags
		vk::ShaderStageFlagBits::eCompute,           // stage
		*shader.handle(),                            // module
		entryPoint                                   // pName
	);
	m_stageSet = true;
	return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::AddDescriptorSetLayout(
	vk::DescriptorSetLayout layout)
{
	m_descriptorSetLayouts.push_back(layout);
	return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::AddPushConstantRange(
	const vk::PushConstantRange& range)
{
	m_pushConstantRanges.push_back(range);
	return *this;
}

vk::raii::Pipeline ComputePipelineBuilder::BuildComputePipeline()
{
	if (!m_stageSet)
	{
		throw std::runtime_error("ComputePipelineBuilder: No shader stage set. Call SetShaderStage() before BuildComputePipeline().");
	}

	// --- Create pipeline layout ---
	vk::PipelineLayoutCreateInfo layoutCreateInfo(
		{},
		m_descriptorSetLayouts,
		m_pushConstantRanges);

	m_pipelineLayout = std::make_unique<vk::raii::PipelineLayout>(
		m_device, layoutCreateInfo);

	// --- Create compute pipeline ---
	vk::ComputePipelineCreateInfo computeCreateInfo(
		{},                // flags
		m_stageInfo,       // stage
		*m_pipelineLayout  // layout
	);

	auto pipeline = vk::raii::Pipeline(m_device, nullptr, computeCreateInfo);

#ifdef _DEBUG
	if (!m_debugName.empty())
	{
		m_device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT(
			vk::ObjectType::ePipeline,
			reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*pipeline)),
			m_debugName.c_str()));
	}
#endif

	return pipeline;
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

ComputePipelineBuilder& ComputePipelineBuilder::SetDebugName(const char* name)
{
	m_debugName = name ? name : "";
	return *this;
}

} // namespace neurus
