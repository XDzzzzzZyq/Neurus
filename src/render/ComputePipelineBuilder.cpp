#include "ComputePipelineBuilder.h"
#include "shaders/ShaderModule.h"

#include <stdexcept>

namespace neurus {

ComputePipelineBuilder::ComputePipelineBuilder(const vk::raii::Device& device)
	: p_device(device)
{
}

ComputePipelineBuilder& ComputePipelineBuilder::SetShaderStage(
	const ShaderModule& shader,
	const char* entryPoint)
{
	// Build the shader stage create info
	p_stageInfo = vk::PipelineShaderStageCreateInfo(
		{},                                          // flags
		vk::ShaderStageFlagBits::eCompute,           // stage
		*shader.handle(),                            // module
		entryPoint                                   // pName
	);
	p_stageSet = true;
	return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::AddDescriptorSetLayout(
	vk::DescriptorSetLayout layout)
{
	p_descriptorSetLayouts.push_back(layout);
	return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::AddPushConstantRange(
	const vk::PushConstantRange& range)
{
	p_pushConstantRanges.push_back(range);
	return *this;
}

vk::raii::Pipeline ComputePipelineBuilder::BuildComputePipeline()
{
	if (!p_stageSet)
	{
		throw std::runtime_error("ComputePipelineBuilder: No shader stage set. Call SetShaderStage() before BuildComputePipeline().");
	}

	// --- Create pipeline layout ---
	vk::PipelineLayoutCreateInfo layoutCreateInfo(
		{},
		p_descriptorSetLayouts,
		p_pushConstantRanges);

	p_pipelineLayout = std::make_unique<vk::raii::PipelineLayout>(
		p_device, layoutCreateInfo);

	// --- Create compute pipeline ---
	vk::ComputePipelineCreateInfo computeCreateInfo(
		{},                // flags
		p_stageInfo,       // stage
		*p_pipelineLayout  // layout
	);

	auto pipeline = vk::raii::Pipeline(p_device, nullptr, computeCreateInfo);

#ifdef _DEBUG
	if (!p_debugName.empty())
	{
		p_device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT(
			vk::ObjectType::ePipeline,
			reinterpret_cast<uint64_t>(static_cast<VkPipeline>(*pipeline)),
			p_debugName.c_str()));
	}
#endif

	return pipeline;
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

ComputePipelineBuilder& ComputePipelineBuilder::SetDebugName(const char* name)
{
	p_debugName = name ? name : "";
	return *this;
}

} // namespace neurus
