#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace neurus {

/**
 * @brief Identifies the type of pipeline — graphics (Geometry) or Compute.
 */
enum class PipelineType
{
	Geometry,
	Compute
};

/**
 * @brief Owned GPU pipeline and its associated pipeline layout.
 *
 * PipelineBuilder returns one of these instead of a raw vk::raii::Pipeline,
 * so the caller never needs to separately manage the pipeline layout or
 * keep a builder alive for layout access.
 *
 * Non-copyable (vk::raii types are move-only), movable.
 *
 * Usage:
 * @code
 *   Pipeline myPipeline = PipelineBuilder()
 *       .AddShaderStage(...)
 *       .BuildComputePipeline(device);
 *
 *   cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *myPipeline.pipeline);
 *   cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
 *                             *myPipeline.pipelineLayout, ...);
 * @endcode
 */
struct Pipeline
{
	vk::raii::Pipeline       pipeline       = nullptr;
	vk::raii::PipelineLayout pipelineLayout = nullptr;
	PipelineType             type           = PipelineType::Compute;
};

} // namespace neurus
