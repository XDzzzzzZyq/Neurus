#pragma once
#include "passes/ComputePass.h"
#include "../shaders/ShaderLibrary.h"
#include "../shaders/ComputeShader.h"
#include <vulkan/vulkan_raii.hpp>
#include <memory>

namespace neurus {
class RenderCache;

struct FXAAPushConstants {
	float rcpWidth, rcpHeight, pad0, pad1;
	float qualitySubpix, edgeThreshold, edgeThresholdMin, pad2;
};

class FXAAPass : public ComputePass {
public:
	FXAAPass(const vk::raii::Device& device,
	         const vk::raii::PhysicalDevice& physicalDevice,
	         uint32_t numSets);
	void Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx) override;
	void WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache) override;
private:
	static DescriptorSetLayout CreateLayout(const vk::raii::Device& d);
	void BuildPipeline(const vk::raii::Device& device, const std::string& name) override;
	static vk::raii::Sampler CreateBilinearSampler(const vk::raii::Device& device);

	std::unique_ptr<ComputeShader> m_shader;
	vk::raii::Sampler              m_bilinearSampler = nullptr;
	bool                           m_hasBilinear     = false;
};
} // namespace neurus
