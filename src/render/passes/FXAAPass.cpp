#include "RenderCache.h"
#include "passes/FXAAPass.h"
#include "../PipelineBuilder.h"
#include "../RenderConfig.h"
#include "Barrier.h"
#include "Image.h"
#include "RenderContext.h"
#include "shaders/ShaderLibrary.h"
#include "shaders/ComputeShader.h"
#include "core/Log.h"
#include <stdexcept>

namespace neurus {

FXAAPass::FXAAPass(const vk::raii::Device& device,
                   const vk::raii::PhysicalDevice& physicalDevice,
                   uint32_t numSets)
	: ComputePass(device, physicalDevice, CreateLayout(device), numSets)
	, m_shader(ShaderLibrary::ParseComputeShader("fxaa", NEURUS_SHADER_DIR "compute/fxaa.comp"))
{
	// Check format features: linear filtering required for sub-pixel texture() resample
	auto fmtProps = physicalDevice.getFormatProperties(vk::Format::eR16G16B16A16Sfloat);
	bool linearOk = (fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)
	                == vk::FormatFeatureFlagBits::eSampledImageFilterLinear;

	if (linearOk)
	{
		m_bilinearSampler = CreateBilinearSampler(device);
		m_hasBilinear = true;
	}

	BuildPipeline(device, "FXAAPass");
	NEURUS_LOG("[FXAAPass] numSets=" << numSets
	           << " shader=" << (m_shader ? "OK" : "FAIL")
	           << " bilinear=" << (linearOk ? "yes" : "NO (fallback to nearest)"));
}

vk::raii::Sampler FXAAPass::CreateBilinearSampler(const vk::raii::Device& device)
{
	vk::SamplerCreateInfo ci(
		{},
		vk::Filter::eLinear,                             // magFilter — bilinear for sub-pixel resample
		vk::Filter::eLinear,                             // minFilter
		vk::SamplerMipmapMode::eNearest,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		0.0f, VK_FALSE, 0.0f, VK_FALSE,
		vk::CompareOp::eAlways,
		0.0f, 0.0f,
		vk::BorderColor::eFloatTransparentBlack,
		VK_FALSE);
	return vk::raii::Sampler(device, ci);
}

DescriptorSetLayout FXAAPass::CreateLayout(const vk::raii::Device& d)
{
	return BuildLayout()
		.AddBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1, vk::DescriptorType::eStorageImage,         vk::ShaderStageFlagBits::eCompute)
		.Build(d);
}

void FXAAPass::BuildPipeline(const vk::raii::Device& device, const std::string& debugName)
{
	if (!m_shader)
		throw std::runtime_error("FXAAPass: shader invalid");
	auto spv = ShaderLibrary::Compile(m_shader->GetStage(ShaderType::COMPUTE),
	                                  ShaderType::COMPUTE, debugName);
	vk::raii::ShaderModule mod(device, vk::ShaderModuleCreateInfo({}, spv));
	vk::PushConstantRange pc(vk::ShaderStageFlagBits::eCompute, 0, sizeof(FXAAPushConstants));
	p_pipelines.push_back(
		PipelineBuilder()
			.AddShaderStage(vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *mod, "main"))
			.SetDebugName(debugName.c_str())
			.AddDescriptorSetLayout(*p_descriptorSetLayout.layout())
			.AddPushConstantRange(pc)
			.BuildComputePipeline(device));
}

void FXAAPass::WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
{
	DescriptorSet& ds = p_descriptorSets[setIndex];

	// Use bilinear sampler for sub-pixel texture() resample (fallback to nearest if unsupported)
	const auto& in = cache.GetAttachment(AttachmentName::ComposedOutput, extent);
	vk::DescriptorImageInfo ii(
		m_hasBilinear ? *m_bilinearSampler : *p_sampler,
		*in.ImageViewHandle(),
		vk::ImageLayout::eShaderReadOnlyOptimal);
	ds.WriteImage(0, ii, vk::DescriptorType::eCombinedImageSampler);

	const auto& out = cache.GetAttachment(AttachmentName::FXAAOutput, extent);
	vk::DescriptorImageInfo oi(nullptr, *out.ImageViewHandle(), vk::ImageLayout::eGeneral);
	ds.WriteImage(1, oi, vk::DescriptorType::eStorageImage);
}

void FXAAPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	const vk::Extent2D extent{ctx.width, ctx.height};
	const uint32_t fi = ctx.frameIndex;
	const auto* cfg = static_cast<const RenderConfig*>(ctx.config);

	WriteDescriptors(fi, extent, cache);

	{
		auto& in = cache.GetAttachment(AttachmentName::ComposedOutput, extent);
		Barrier::Transition(cmdBuf, in, ImageState::ColorShaderRead);
		auto& out = cache.GetAttachment(AttachmentName::FXAAOutput, extent);
		Barrier::Transition(cmdBuf, out, ImageState::ShaderWrite);
	}

	FXAAPushConstants pc = {};
	pc.rcpWidth       = 1.0f / float(extent.width);
	pc.rcpHeight      = 1.0f / float(extent.height);
	pc.qualitySubpix  = cfg ? cfg->r_fxaa_subpix            : 0.75f;
	pc.edgeThreshold  = cfg ? cfg->r_fxaa_edge_threshold    : 0.166f;
	pc.edgeThresholdMin = cfg ? cfg->r_fxaa_edge_threshold_min : 0.0833f;

	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *p_pipelines[0].pipeline);
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          *p_pipelines[0].pipelineLayout, 0,
	                          {p_descriptorSets[fi].handle()}, {});
	cmdBuf.pushConstants<FXAAPushConstants>(
		*p_pipelines[0].pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pc);

	const uint32_t gx = (extent.width  + 15) / 16;
	const uint32_t gy = (extent.height + 15) / 16;
	cmdBuf.dispatch(gx, gy, 1);

	{
		auto& out = cache.GetAttachment(AttachmentName::FXAAOutput, extent);
		Barrier::Transition(cmdBuf, out, ImageState::TransferSrc);
	}
}

} // namespace neurus
