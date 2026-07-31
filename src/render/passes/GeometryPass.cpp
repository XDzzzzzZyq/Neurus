/**
 * @file GeometryPass.cpp
 * @brief G-Buffer geometry pass implementation.
 */

#include "passes/GeometryPass.h"

#include "passes/Pass.h"
#include "RenderCache.h"
#include "RenderContext.h"
#include "render/Barrier.h"
#include "PipelineBuilder.h"
#include "shaders/ShaderLibrary.h"
#include "shaders/RenderShader.h"
#include "resources/ShaderGPU.h"

#include "core/Log.h"

#include "../resources/MeshGPU.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GeometryPass::GeometryPass(const vk::raii::Device& device,
                           const vk::raii::PhysicalDevice& physicalDevice)
	// --- Descriptor set layout ---
	: p_cameraLayout(CreateCameraLayout(device))
	// --- Camera UBO (host-visible for per-frame memcpy update) ---
	, p_cameraUBO(device, physicalDevice, "CameraUBO")
	// --- Descriptor pool (1 set, 1 UBO) ---
	, p_descriptorPool(device,
	                   1,
	                   DescriptorPool::CalculatePoolSizes({&p_cameraLayout}, 1))
	// --- Camera descriptor set (allocated from pool) ---
	, p_cameraDescriptorSet(std::move(
	      p_descriptorPool.Allocate(p_cameraLayout, 1).front()))
	// --- Self-load shaders via ShaderLibrary ---
	, p_shader(
		ShaderLibrary::LoadRenderShader("GeometryPass",
		                                 "res/shaders/render/gbuffer.vert",
		                                 "res/shaders/render/gbuffer.frag"))
{
	p_device = &device;
	p_physicalDevice = &physicalDevice;

	// --- Write camera UBO to descriptor set ---
	p_cameraDescriptorSet.WriteBuffer(0, p_cameraUBO.GetDescriptorInfo(),
	                                  vk::DescriptorType::eUniformBuffer);

#ifdef _DEBUG
	p_cameraDescriptorSet.SetDebugName("GeometryPass_CameraSet");
#endif

	// --- Build vertex input layout ---
	p_vertexLayout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);   // pos @ 0
	p_vertexLayout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);  // normal @ 12
	p_vertexLayout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);      // uv @ 24

	// --- Create graphics pipeline from self-loaded shader ---
	BuildPipeline(device, "GeometryPass");

	NEURUS_LOG("[GeometryPass] shader=" << (p_shader ? "OK" : "FAIL")
	          << " colorAttachments=5"
	          << " depthAttachments=1"
	          << " vertexStride=" << p_vertexLayout.GetStride());
}

// ---------------------------------------------------------------------------
// Descriptor set layout factory
// ---------------------------------------------------------------------------

DescriptorSetLayout GeometryPass::CreateCameraLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		.AddBinding(0,
		            vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eVertex)
		.Build(device);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

void GeometryPass::BuildPipeline(const vk::raii::Device& device,
                                  const std::string& debugName)
{
	// --- Guard: shader must be valid ---
	if (!p_shader)
	{
		throw std::runtime_error("GeometryPass: Render shader not loaded or invalid");
	}

	// --- Compile and create temporary shader modules ---
	auto vertSpv = ShaderLibrary::Compile(p_shader->GetStage(ShaderType::VERTEX),
	                                      ShaderType::VERTEX, debugName + "_vert");
	auto fragSpv = ShaderLibrary::Compile(p_shader->GetStage(ShaderType::FRAGMENT),
	                                      ShaderType::FRAGMENT, debugName + "_frag");
	ShaderGPU vertGPU(device, vk::ShaderStageFlagBits::eVertex, vertSpv);
	ShaderGPU fragGPU(device, vk::ShaderStageFlagBits::eFragment, fragSpv);

	// --- G-Buffer colour attachment formats ---
	std::vector<vk::Format> colorFormats = {
		vk::Format::eR16G16B16A16Sfloat,  // Position
		vk::Format::eR16G16B16A16Sfloat,  // Normal
		vk::Format::eR8G8B8A8Srgb,        // Albedo
		vk::Format::eR8G8B8A8Unorm,       // MetallicRoughness
		vk::Format::eR32Uint,             // IDBuffer
	};

	// --- Colour blend: 5 attachments, no blending (write all channels) ---
	vk::PipelineColorBlendAttachmentState blendState;
	blendState.blendEnable = VK_FALSE;
	blendState.colorWriteMask =
		vk::ColorComponentFlagBits::eR |
		vk::ColorComponentFlagBits::eG |
		vk::ColorComponentFlagBits::eB |
		vk::ColorComponentFlagBits::eA;

	// --- Push constant range: model + normalMatrix + objectID (vertex + fragment stages) ---
	std::vector<vk::PushConstantRange> pushConstantRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		                      0,
		                      sizeof(MeshPushConstants))
	};

	// --- Descriptor set layout handles ---
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts = {
		*p_cameraLayout.layout()
	};

	// --- Build pipeline via PipelineBuilder ---
	PipelineBuilder builder;
	p_pipelines.push_back(
		builder
			.SetDebugName(debugName.c_str())
			.AddShaderStage(vertGPU.GetStageCreateInfo())
			.AddShaderStage(fragGPU.GetStageCreateInfo())
			.SetVertexInput(p_vertexLayout)
			.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
			.SetRasterization(vk::PolygonMode::eFill,
			                  vk::CullModeFlagBits::eNone,
			                  vk::FrontFace::eClockwise)
			.SetMultisampling()
			.SetDepthStencil(true, true, vk::CompareOp::eLess)
			.AddColorBlendAttachment(blendState)
			.AddColorBlendAttachment(blendState)
			.AddColorBlendAttachment(blendState)
			.AddColorBlendAttachment(blendState)
			.AddColorBlendAttachment(blendState)
			.SetColorFormats(colorFormats)
			.SetDepthFormat(vk::Format::eD32Sfloat)
			.SetDescriptorSetLayouts(descriptorSetLayouts)
			.SetPushConstantRanges(pushConstantRanges)
			.BuildGraphicsPipeline(device));
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void GeometryPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	// --- Cast scene UID to Scene* for access to Scene-specific members ---
	const auto* scene = static_cast<const Scene*>(ctx.scene);

	// --- Extract per-frame context ---
	const Camera* cam = scene->GetActiveCamera();
	const CameraUBOData cameraData{
		cam->GetProjectionMatrix() * cam->GetViewMatrix(),
		cam->GetViewMatrix()
	};
	const vk::Extent2D renderExtent{ctx.width, ctx.height};

	// --- 1. Upload camera data to UBO ---
	p_cameraUBO.Upload(cameraData);

	// --- 2. Collect G-Buffer attachment image views ---
	const std::array<AttachmentName, 5> gBufferColorAttachments = {
		AttachmentName::Position,
		AttachmentName::Normal,
		AttachmentName::Albedo,
		AttachmentName::MetallicRoughness,
		AttachmentName::IDBuffer,
	};

	std::vector<vk::ImageView> colorViews;
	colorViews.reserve(5);
	for (const auto& attName : gBufferColorAttachments)
	{
		auto& att = cache.GetAttachment(attName, renderExtent);
		Barrier::Transition(cmdBuf, att, ImageState::ColorAttachment);
		colorViews.push_back(*att.ImageViewHandle());
	}

	auto& depthAtt = cache.GetAttachment(AttachmentName::Depth, renderExtent);
	Barrier::Transition(cmdBuf, depthAtt, ImageState::DepthAttachment);
	vk::ImageView depthView = *depthAtt.ImageViewHandle();

	// --- 3. Begin G-Buffer dynamic rendering pass ---
	const auto clearValues = Pass::PresetClearValues(Pass::PassType::G_BUFFER);

	BeginPass(cmdBuf, colorViews, &depthView, clearValues, renderExtent);

	// --- 4. Set viewport and scissor (dynamic state) ---
	const vk::Viewport viewport(0.0f, 0.0f,
	                            static_cast<float>(renderExtent.width),
	                            static_cast<float>(renderExtent.height),
	                            0.0f, 1.0f);
	cmdBuf.setViewport(0, viewport);

	const vk::Rect2D scissor({0, 0}, renderExtent);
	cmdBuf.setScissor(0, scissor);

	// --- 5. Bind pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *p_pipelines[0].pipeline);

	// --- 6. Bind camera descriptor set (set 0) ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
	                          *p_pipelines[0].pipelineLayout,
	                          0,
	                          {p_cameraDescriptorSet.handle()},
	                          {});

	// --- 7. Draw each mesh from scene.mesh_list ---
	if (scene)
	{
		for (const auto& [id, mesh] : scene->mesh_list)
		{
			if (!mesh || !mesh->o_mesh) continue;
			if (!mesh->is_viewport || !mesh->is_rendered) continue;

			MeshGPU* gpuPtr = cache.GetMeshGPU(mesh->GetObjectID());
			if (!gpuPtr)
			{
				NEURUS_ERR("[GeometryPass] GetMeshGPU returned nullptr for objectId=" << mesh->GetObjectID());
				continue;
			}

			const glm::mat4 model = mesh->GetModelMatrix();
			const glm::mat4 normalMat = glm::mat4(mesh->GetNormalMatrix());

			MeshPushConstants pushConstants{model, normalMat,
			                               static_cast<uint32_t>(mesh->GetObjectID())};
			cmdBuf.pushConstants<MeshPushConstants>(*p_pipelines[0].pipelineLayout,
			                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
			                                    0, pushConstants);

			const vk::DeviceSize vbOffset = 0;
			cmdBuf.bindVertexBuffers(0, gpuPtr->vertexBuffer->buffer(), vbOffset);
			cmdBuf.bindIndexBuffer(gpuPtr->indexBuffer->buffer(), 0, vk::IndexType::eUint32);
			cmdBuf.drawIndexed(gpuPtr->indexCount, 1, 0, 0, 0);
		}
	}

	// --- 8. End dynamic rendering pass ---
	EndPass(cmdBuf);
}

// ---------------------------------------------------------------------------
// BeginPass / EndPass (G_BUFFER-specific, moved from RenderPassManager)
// ---------------------------------------------------------------------------

void GeometryPass::BeginPass(vk::CommandBuffer cmdBuf,
                              std::span<const vk::ImageView> colorImageViews,
                              const vk::ImageView* pDepthImageView,
                              std::span<const vk::ClearValue> clearValues,
                              vk::Extent2D renderExtent)
{
	const uint32_t colorCount = static_cast<uint32_t>(colorImageViews.size());

	if (colorCount != Pass::ColorAttachmentCount(Pass::PassType::G_BUFFER))
	{
		throw std::invalid_argument(
			"GeometryPass::BeginPass: expected " + std::to_string(Pass::ColorAttachmentCount(Pass::PassType::G_BUFFER)) + " color attachments, got "
			+ std::to_string(colorCount));
	}

	const bool depthProvided = (pDepthImageView != nullptr);
	if (!depthProvided)
	{
		throw std::invalid_argument(
			"GeometryPass::BeginPass: depth attachment required for G_BUFFER pass");
	}

	// --- Build color attachment infos ---
	const auto colorLoadOp  = Pass::ColorLoadOpFor(Pass::PassType::G_BUFFER);
	const auto colorStoreOp = Pass::ColorStoreOpFor(Pass::PassType::G_BUFFER);

	std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos;
	colorAttachmentInfos.reserve(colorCount);

	for (uint32_t i = 0; i < colorCount; ++i)
	{
		colorAttachmentInfos.push_back(vk::RenderingAttachmentInfo(
			colorImageViews[i],
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ResolveModeFlagBits::eNone,
			nullptr,
			vk::ImageLayout::eUndefined,
			colorLoadOp,
			colorStoreOp,
			(i < static_cast<uint32_t>(clearValues.size())) ? clearValues[i] : vk::ClearValue{}));
	}

	// --- Build depth attachment info ---
	const auto depthLoadOp  = Pass::DepthLoadOpFor(Pass::PassType::G_BUFFER);
	const auto depthStoreOp = Pass::DepthStoreOpFor(Pass::PassType::G_BUFFER);

	const size_t depthClearIndex = colorCount;

	vk::RenderingAttachmentInfo depthAttachmentInfo(
		*pDepthImageView,
		vk::ImageLayout::eDepthStencilAttachmentOptimal,
		vk::ResolveModeFlagBits::eNone,
		nullptr,
		vk::ImageLayout::eUndefined,
		depthLoadOp,
		depthStoreOp,
		(depthClearIndex < clearValues.size()) ? clearValues[depthClearIndex] : vk::ClearValue{});

	// --- Build rendering info ---
	vk::RenderingInfo renderingInfo(
		{},
		vk::Rect2D({0, 0}, renderExtent),
		1,
		0,
		colorAttachmentInfos,
		&depthAttachmentInfo,
		nullptr);

	cmdBuf.beginRendering(renderingInfo);
}

void GeometryPass::EndPass(vk::CommandBuffer cmdBuf)
{
	cmdBuf.endRendering();
}

PassIO GeometryPass::GetIO() const
{
	// GeometryPass is a rasterization pass writing the G-Buffer MRT + depth.
	// It has no image reads (camera data comes via a UBO). Binding metadata is
	// unused — the pass self-manages its render pass and attachments; only the
	// resource identities drive RenderGraph edges to SSAO / Lighting / Gizmo.
	PassIO io;
	io.name  = "GeometryPass";
	io.writes = {
		{AttachmentName::Position},
		{AttachmentName::Normal},
		{AttachmentName::Albedo},
		{AttachmentName::MetallicRoughness},
		{AttachmentName::IDBuffer},
		{AttachmentName::Depth},
	};
	return io;
}

} // namespace neurus
