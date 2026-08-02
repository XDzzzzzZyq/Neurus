/**
 * @file GizmoPass.cpp
 * @brief Selected-object edge highlight compute pass implementation.
 */

#include "RenderCache.h"
#include "passes/GizmoPass.h"

#include "../PipelineBuilder.h"
#include "Image.h"
#include "render/Barrier.h"
#include "render/render_graph/DescriptorBinder.h"
#include "RenderContext.h"
#include "shaders/ShaderLibrary.h"
#include "shaders/ComputeShader.h"

#include "core/Log.h"

#include "scene/Scene.h"

#include <stdexcept>
#include <string>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GizmoPass::GizmoPass(const vk::raii::Device& device,
                     const vk::raii::PhysicalDevice& physicalDevice,
                     uint32_t numSets)
	: ComputePass(device, physicalDevice,
	              GizmoPass::CreateDescriptorSetLayout(device), numSets)
	// --- Self-load compute shader via ShaderLibrary ---
	, p_shader(
		ShaderLibrary::LoadComputeShader("gizmo_highlight",
		                                  "res/shaders/compute/gizmo_highlight.comp"))
{
	// --- Create pipeline from self-loaded shader ---
	BuildPipeline(device, "GizmoPass");

	NEURUS_LOG("[GizmoPass] numSets=" << numSets
	           << " shader=" << (p_shader ? "OK" : "FAIL"));

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets; ++i)
	{
		const std::string dsName = "GizmoPass_Set" + std::to_string(i);
		p_descriptorSets[i].SetDebugName(dsName.c_str());
	}
#endif
}

// ---------------------------------------------------------------------------
// Descriptor set layout
// ---------------------------------------------------------------------------

DescriptorSetLayout GizmoPass::CreateDescriptorSetLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		// IDBuffer input (combined image sampler, usampler2D)
		.AddBinding(0,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// GizmoHighlight output (storage image, R8)
		.AddBinding(1,
		            vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(device);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

void GizmoPass::BuildPipeline(const vk::raii::Device& device,
                               const std::string& debugName)
{
	// --- Guard: shader must be valid ---
	if (!p_shader)
	{
		throw std::runtime_error("GizmoPass: Compute shader not loaded or invalid");
	}

	// --- Compile and create temporary shader module ---
	auto spv = ShaderLibrary::Compile(p_shader->GetStage(ShaderType::COMPUTE),
	                                  ShaderType::COMPUTE, debugName);
	vk::ShaderModuleCreateInfo smCI({}, spv);
	vk::raii::ShaderModule module(device, smCI);
	vk::PipelineShaderStageCreateInfo stageCI({}, vk::ShaderStageFlagBits::eCompute, *module, "main");

	// --- Push constant range (1 uint = 4 bytes) ---
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		sizeof(uint32_t));  // activeObjectId

	// --- Build compute pipeline ---
	PipelineBuilder builder;
	p_pipelines.push_back(
		builder.AddShaderStage(stageCI)
			.SetDebugName(debugName.c_str())
			.AddDescriptorSetLayout(*p_descriptorSetLayout.layout())
			.AddPushConstantRange(pushRange)
			.BuildComputePipeline(device));
}

// ---------------------------------------------------------------------------
// I/O declaration
// ---------------------------------------------------------------------------

PassIO GizmoPass::GetIO() const
{
	PassIO io;
	io.name = "GizmoPass";
	io.reads = {
		// IDBuffer sampled as usampler2D at binding 0.
		{AttachmentName::IDBuffer, 0,
		 vk::DescriptorType::eCombinedImageSampler,
		 vk::ImageLayout::eShaderReadOnlyOptimal},
	};
	io.writes = {
		// GizmoHighlight written as a storage image at binding 1.
		{AttachmentName::GizmoHighlight, 1,
		 vk::DescriptorType::eStorageImage,
		 vk::ImageLayout::eGeneral},
	};
	return io;
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void GizmoPass::WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
{
	// Image bindings are derived from GetIO() and applied by DescriptorBinder,
	// so the binding list lives in exactly one place (GetIO).
	const PassIO io = GetIO();
	DescriptorBinder::BindImages(p_descriptorSets[setIndex],
	                             io.reads, io.writes,
	                             cache, extent, *p_sampler);
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void GizmoPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	const vk::Extent2D renderExtent{ctx.width, ctx.height};
	const uint32_t    frameIndex   = ctx.frameIndex;

	// --- 0. No per-frame uploads needed — activeObjectId read directly from ctx ---

	// --- 1. Write descriptor set for this frame slot ---
	WriteDescriptors(frameIndex, renderExtent, cache);

	// --- 2. Transition IDBuffer to ShaderRead and GizmoHighlight to ShaderWrite ---
	{
		auto& idAtt = cache.GetAttachment(AttachmentName::IDBuffer, renderExtent);
		Barrier::Transition(cmdBuf, idAtt, ImageState::ColorShaderRead);

		auto& gizmoAtt = cache.GetAttachment(AttachmentName::GizmoHighlight, renderExtent);
		Barrier::Transition(cmdBuf, gizmoAtt, ImageState::ShaderWrite);
	}

	// --- 3. Bind compute pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *p_pipelines[0].pipeline);

	// --- 4. Bind descriptor set ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          *p_pipelines[0].pipelineLayout,
	                          0,                                    // firstSet
	                          {p_descriptorSets[frameIndex].handle()},
	                          {});

	// --- 5. Push constants (uint32_t activeObjectId) ---
	{
		// Query the active selection from the scene (avoids redundant field in RenderContext)
		uint32_t activeObjectId = 0;
		const auto* scene = static_cast<const Scene*>(ctx.scene);
		const auto* activeObj = scene->selections.GetActiveObject();
		if (activeObj)
			activeObjectId = static_cast<uint32_t>(activeObj->GetObjectID());

		cmdBuf.pushConstants<uint32_t>(
			*p_pipelines[0].pipelineLayout,
			vk::ShaderStageFlagBits::eCompute,
			0,
			activeObjectId);
	}

	// --- 6. Dispatch ---
	const uint32_t groupCountX = (renderExtent.width  + 15) / 16;
	const uint32_t groupCountY = (renderExtent.height + 15) / 16;
	++m_dispatches;
	cmdBuf.dispatch(groupCountX, groupCountY, 1);

	// --- 7. Transition GizmoHighlight output: General → ShaderRead for downstream passes ---
	{
		auto& gizmoAtt = cache.GetAttachment(AttachmentName::GizmoHighlight, renderExtent);
		Barrier::Transition(cmdBuf, gizmoAtt, ImageState::ColorShaderRead);
	}
}

} // namespace neurus
