#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <vector>

namespace neurus {

struct ShaderGPU
{
	ShaderGPU(const vk::raii::Device& device,
	          vk::ShaderStageFlagBits stage,
	          const std::vector<uint32_t>& spirv,
	          const char* entryPoint = "main")
	    : module(std::make_unique<vk::raii::ShaderModule>(
	          device, vk::ShaderModuleCreateInfo({}, spirv)))
	    , stageFlag(stage)
	    , entryPoint(entryPoint)
	{
	}

	ShaderGPU(const ShaderGPU&) = delete;
	ShaderGPU& operator=(const ShaderGPU&) = delete;
	ShaderGPU(ShaderGPU&&) noexcept = default;
	ShaderGPU& operator=(ShaderGPU&&) noexcept = default;

	vk::PipelineShaderStageCreateInfo GetStageCreateInfo() const
	{
		return vk::PipelineShaderStageCreateInfo({}, stageFlag, **module, entryPoint);
	}

	std::unique_ptr<vk::raii::ShaderModule> module;
	vk::ShaderStageFlagBits stageFlag;
	const char* entryPoint;
};

} // namespace neurus
