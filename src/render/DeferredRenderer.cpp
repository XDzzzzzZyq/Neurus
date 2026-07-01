/**
 * @file DeferredRenderer.cpp
 * @brief Deferred rendering pipeline implementation.
 */

#include "DeferredRenderer.h"

#include "Barrier.h"
#include "RenderCache.h"
#include "passes/GeometryPass.h"
#include "passes/IBLPass.h"
#include "passes/LightingPass.h"
#include "RenderContext.h"
#include "Image.h"
#include "Texture.h"
#include "Screenshot.h"
#include "passes/SSAOPass.h"
#include "passes/ShadowDepthPass.h"
#include "passes/ShadowIntensityPass.h"
#include "Swapchain.h"
#include "passes/SyncObjects.h"
#include "buffers/GPUBuffer.h"
#include "buffers/VertexBuffer.h"
#include "buffers/IndexBuffer.h"
#include "shaders/ShaderModule.h"
#include "ComputePipelineBuilder.h"
#include "DescriptorManager.h"
#include "asset/ImageData.h"
#include "scene/Light.h"

// Generated SPIR-V shader headers
#include "gbuffer.vert.h"
#include "gbuffer.frag.h"
#include "pbr_lighting.comp.h"
#include "ssao.comp.h"
#include "irradiance_conv.comp.h"
#include "importance_samp.comp.h"
#include "shadow_depth.frag.h"
#include "c2e.comp.h"
#include "shadow_eval.comp.h"

#include <glm/gtc/matrix_transform.hpp>

#include "Log.h"

#include "scene/Camera.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DeferredRenderer::DeferredRenderer(const vk::raii::Device& device,
                                    const vk::raii::PhysicalDevice& physicalDevice,
                                    vk::Queue graphicsQueue,
                                    uint32_t queueFamilyIndex,
                                    const vk::raii::SurfaceKHR& surface,
                                    uint32_t width,
                                    uint32_t height)
	: r_device(device)
	, r_physicalDevice(physicalDevice)
	, r_graphicsQueue(graphicsQueue)
	, r_queueFamilyIndex(queueFamilyIndex)
	, r_commandPool(createCommandPool(device, queueFamilyIndex))
{
	// --- 1. Create swapchain ---
	r_swapchain = std::make_unique<Swapchain>(physicalDevice, device, surface, width, height);
	const auto extent = r_swapchain->extent();

	// --- 2. Create G-Buffer attachment cache (lazy creation on first access) ---
	r_renderCache = std::make_unique<RenderCache>(device, physicalDevice);

	// --- 3. Create geometry pass ---
	{
		auto geoPass = std::make_unique<GeometryPass>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));
		r_geometryPass = geoPass.get();
		r_passes.push_back(std::move(geoPass));
	}

	// --- 5. Create lighting pass ---
	{
		auto lightPass = std::make_unique<LightingPass>(
			device, physicalDevice,
			kMaxFramesInFlight,
			r_graphicsQueue, r_queueFamilyIndex,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
		r_lightingPass = lightPass.get();
		r_passes.push_back(std::move(lightPass));
	}

	// --- 6. Create SSAO pass ---
	{
		auto ssaoPass = std::make_unique<SSAOPass>(
			device, physicalDevice,
			kMaxFramesInFlight,
			r_graphicsQueue, r_queueFamilyIndex,
			ssao_comp_spv, sizeof(ssao_comp_spv));
		r_ssaoPass = ssaoPass.get();
		r_passes.push_back(std::move(ssaoPass));
	}

	// --- 7. Create IBL pass (pure compute service) ---
	{
		auto iblPass = std::make_unique<IBLPass>(
			device, physicalDevice,
			r_graphicsQueue, r_queueFamilyIndex,
			irradiance_conv_comp_spv, sizeof(irradiance_conv_comp_spv),
			importance_samp_comp_spv, sizeof(importance_samp_comp_spv));
		r_iblPass = iblPass.get();
		r_passes.push_back(std::move(iblPass));
		NEURUS_LOG("[DeferredRenderer] IBLPass created");
	}

	// --- 8. Create shadow depth pass (cubemap depth from light's POV) ---
	{
		auto shadowDepth = std::make_unique<ShadowDepthPass>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			ShadowDepthPass::kDefaultResolution);
		r_shadowDepthPass = shadowDepth.get();
		r_passes.push_back(std::move(shadowDepth));
		NEURUS_LOG("[DeferredRenderer] ShadowDepthPass created");
	}

	// --- 8b. Create shadow intensity pass (per-pixel shadow evaluation from cubemap) ---
	{
		auto shadowIntensity = std::make_unique<ShadowIntensityPass>(
			device, physicalDevice,
			kMaxFramesInFlight,
			graphicsQueue, queueFamilyIndex,
			shadow_eval_comp_spv, sizeof(shadow_eval_comp_spv));
		r_shadowIntensityPass = shadowIntensity.get();
		r_passes.push_back(std::move(shadowIntensity));
		NEURUS_LOG("[DeferredRenderer] ShadowIntensityPass created");
	}

	// --- 9. Allocate command buffers (one per swapchain image, reused) ---
	uint32_t imageCount = r_swapchain->imageCount();

	// Verify the swapchain supports TRANSFER_DST for the blit composite path
	const bool hasTransferDst = (r_swapchain->actualImageUsage() & vk::ImageUsageFlagBits::eTransferDst) != vk::ImageUsageFlags{};
	if (!hasTransferDst)
	{
		throw std::runtime_error(
			"Swapchain surface does not support VK_IMAGE_USAGE_TRANSFER_DST_BIT.\n"
			"DeferredRenderer requires TRANSFER_DST for the HDRColor-to-swapchain blit composite.\n"
			"Try running on a GPU/driver that supports this usage flag for the surface format.");
	}

	vk::CommandBufferAllocateInfo cmdBufAlloc(*r_commandPool, vk::CommandBufferLevel::ePrimary, imageCount);
	r_commandBuffers = vk::raii::CommandBuffers(device, cmdBufAlloc);

	// --- Set debug names for command buffers ---
#ifdef _DEBUG
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		char nameBuf[32];
		snprintf(nameBuf, sizeof(nameBuf), "DeferredRenderer::FrameCmd[%u]", i);
		const vk::DebugUtilsObjectNameInfoEXT nameInfo(
			vk::ObjectType::eCommandBuffer,
			reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*r_commandBuffers[i])),
			nameBuf);
		device.setDebugUtilsObjectNameEXT(nameInfo);
		NEURUS_LOG("[DeferredRenderer] CmdBuf[" << i << "] handle=0x"
		          << std::hex << reinterpret_cast<uint64_t>(
		                 static_cast<VkCommandBuffer>(*r_commandBuffers[i]))
		          << std::dec << " name='" << nameBuf << "'");
	}
#endif

	// --- 10. Create sync objects ---
	for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
	{
		r_inFlightFences.emplace_back(device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
		r_imageAvailableSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
	}
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		r_renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
	}

	NEURUS_LOG("[DeferredRenderer] " << extent.width << "x" << extent.height
	          << " swapchainImages=" << imageCount
	          << " transferDst=" << (hasTransferDst ? "yes" : "no"));
}

DeferredRenderer::~DeferredRenderer()
{
	WaitIdle();
	// vk::raii destructors clean up automatically in reverse declaration order.
	// r_passes vector destroys all passes in construction order (GeometryPass → LightingPass →
	//   SSAOPass → IBLPass), before RenderCache.
}

// ---------------------------------------------------------------------------
// Light upload delegation
// ---------------------------------------------------------------------------

void DeferredRenderer::UploadLights(const Scene& scene)
{
	// --- Collect shadow-casting point light UIDs (allocated first, indices 0..N-1) ---
	std::vector<int32_t> pointShadowUIDs;
	// --- Collect shadow-casting sun light UIDs (allocated after point lights, indices N..N+M-1) ---
	std::vector<int32_t> sunShadowUIDs;

	if (r_shadowDepthPass)
	{
		for (const auto& [id, light] : scene.light_list)
		{
			if (!light) continue;
			if (light->light_type == LightType::POINTLIGHT && light->use_shadow)
			{
				pointShadowUIDs.push_back(id);
			}
			else if (light->light_type == LightType::SUNLIGHT && light->use_shadow)
			{
				sunShadowUIDs.push_back(id);
			}
		}
	}

	// Sort each group for deterministic shadowMapIndex assignment
	std::sort(pointShadowUIDs.begin(), pointShadowUIDs.end());
	std::sort(sunShadowUIDs.begin(), sunShadowUIDs.end());

	// --- Build combined shadow index map: point lights first, sun lights second ---
	// MAX_SHADOW_LIGHTS=4 is shared between point and sun; point lights get priority
	constexpr int kMaxShadowLayers = 4;
	std::unordered_map<int32_t, int> uidToShadowIndex;

	int nextIndex = 0;
	for (const auto uid : pointShadowUIDs)
	{
		if (nextIndex >= kMaxShadowLayers) break;
		uidToShadowIndex[uid] = nextIndex++;
	}
	for (const auto uid : sunShadowUIDs)
	{
		if (nextIndex >= kMaxShadowLayers) break;
		uidToShadowIndex[uid] = nextIndex++;
	}

	// --- Upload lights to GPU, assigning shadowMapIndex per light ---
	if (r_lightingPass)
	{
		r_lightingPass->UploadLights(scene, &uidToShadowIndex);
		r_lightingPass->UploadSunLights(scene, &uidToShadowIndex);
	}
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

vk::raii::CommandPool DeferredRenderer::createCommandPool(const vk::raii::Device& device,
                                                           uint32_t queueFamilyIndex)
{
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndex);
	return vk::raii::CommandPool(device, poolCI);
}

// ---------------------------------------------------------------------------
// Render item construction
// ---------------------------------------------------------------------------

GeometryRenderItem DeferredRenderer::buildRenderItem(const Mesh& mesh) const
{
	GeometryRenderItem item = {};

	const VertexBuffer* vb = mesh.GetVertexBuffer();
	const IndexBuffer* ib = mesh.GetIndexBuffer();
	if (!vb || !ib)
	{
		return item;  // Not uploaded to GPU - return default (no draw will occur)
	}

	item.vertexBuffer = vb->buffer();
	item.indexBuffer = ib->buffer();
	item.indexCount = mesh.GetGPUIndexCount();
	item.indexType = vk::IndexType::eUint32;

	// Identity model matrix (sphere at origin).
	// The icosphere OBJ has radius ~7.44 - scale down to fit the view
	// frustum (camera at ~5 units from origin).
	const glm::mat4 sphereScale = glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));
	item.pushConstants.model = sphereScale;
	item.pushConstants.normalMatrix = glm::transpose(glm::inverse(sphereScale));

	return item;
}

// ---------------------------------------------------------------------------
// GenerateIBL - wrapper for Editor (avoids cross-layer include of IBLPass.h)
// ---------------------------------------------------------------------------

void DeferredRenderer::GenerateIBL(const Image& equirectImage,
                                    Image& diffuseOut,
                                    Image& specularOut)
{
	r_iblPass->Generate(equirectImage, diffuseOut, specularOut);
}

// ---------------------------------------------------------------------------
// DrawFrame - main render loop entry point
// ---------------------------------------------------------------------------

void DeferredRenderer::DrawFrame()
{
	// Fallback camera matching the old hardcoded defaults.
	Camera fallbackCam;
	fallbackCam.SetCamPos(glm::vec3(0.0f, -5.0f, 2.0f));
	fallbackCam.cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);

	auto& fence = r_inFlightFences[r_currentFrame];
	auto& imageAvailable = r_imageAvailableSemaphores[r_currentFrame];

	// --- Wait for this frame slot's fence ---
	if (r_device.waitForFences(*fence, VK_TRUE, kFenceTimeoutNs) != vk::Result::eSuccess)
	{
		return;  // Timeout - skip this frame
	}

	// --- Acquire next swapchain image ---
	uint32_t imageIndex = 0;
	bool skipFrame = false;
	try
	{
		imageIndex = r_swapchain->AcquireNextImage(imageAvailable);
		r_lastImageIndex = imageIndex;
	}
	catch (const vk::OutOfDateKHRError&)
	{
		NEURUS_ERR("AcquireNextImage: swapchain out of date");
		skipFrame = true;
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("AcquireNextImage failed: " << e.what());
		skipFrame = true;
	}

	// --- Handle swapchain recreation (from out-of-date acquire or external resize) ---
	if (r_swapchain->generation() != r_swapchainGeneration)
	{
		recreateSwapchain();
		skipFrame = true;
	}

	if (skipFrame)
	{
		// Don't advance r_currentFrame - retry same slot next frame
		return;
	}

	// Only reset fence after successful acquire
	r_device.resetFences(*fence);

	// --- Record and submit (reuse pre-allocated command buffer) ---
	vk::CommandBuffer cmdBufRaw = *r_commandBuffers[imageIndex];

	// No-args DrawFrame is deprecated and used only as camera-fallback.
	// Pass empty render items (no geometry drawn) - the fallback exists only
	// to prevent a crash when no camera is configured.
	recordFrame(r_commandBuffers[imageIndex], imageIndex, fallbackCam, {}, nullptr);

	auto& renderFinished = r_renderFinishedSemaphores[imageIndex];
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo(*imageAvailable, waitStage, cmdBufRaw, *renderFinished);
	r_graphicsQueue.submit(submitInfo, *fence);

	// --- Present ---
	bool presentFailed = false;
	try
	{
		r_swapchain->Present(renderFinished, imageIndex, r_graphicsQueue);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Present failed: " << e.what());
		presentFailed = true;
	}

	// --- Handle swapchain recreation after present ---
	if (r_swapchain->generation() != r_swapchainGeneration)
	{
		recreateSwapchain();
		presentFailed = true;
	}

	if (presentFailed)
	{
		// Don't advance frame - retry same slot next iteration
		return;
	}

	r_currentFrame = (r_currentFrame + 1) % kMaxFramesInFlight;
}

void DeferredRenderer::DrawFrame(const Scene& scene)
{
	const Camera* activeCam = scene.GetActiveCamera();
	if (!activeCam)
	{
		NEURUS_ERR("DrawFrame(Scene): No active camera in scene, falling back to default camera");
		DrawFrame();
		return;
	}

	auto& fence = r_inFlightFences[r_currentFrame];
	auto& imageAvailable = r_imageAvailableSemaphores[r_currentFrame];

	// --- Wait for this frame slot's fence ---
	if (r_device.waitForFences(*fence, VK_TRUE, kFenceTimeoutNs) != vk::Result::eSuccess)
	{
		return;  // Timeout - skip this frame
	}

	// --- Acquire next swapchain image ---
	uint32_t imageIndex = 0;
	bool skipFrame = false;
	try
	{
		imageIndex = r_swapchain->AcquireNextImage(imageAvailable);
		r_lastImageIndex = imageIndex;
	}
	catch (const vk::OutOfDateKHRError&)
	{
		NEURUS_ERR("AcquireNextImage: swapchain out of date");
		skipFrame = true;
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("AcquireNextImage failed: " << e.what());
		skipFrame = true;
	}

	// --- Handle swapchain recreation (from out-of-date acquire or external resize) ---
	if (r_swapchain->generation() != r_swapchainGeneration)
	{
		recreateSwapchain();
		skipFrame = true;
	}

	if (skipFrame)
	{
		return;
	}

	// Only reset fence after successful acquire
	r_device.resetFences(*fence);

	// --- Build render items from scene meshes (querying mesh GPU buffers directly) ---
	std::vector<GeometryRenderItem> renderItems;
	renderItems.reserve(scene.mesh_list.size());
	for (const auto& [id, mesh] : scene.mesh_list)
	{
		if (!mesh || !mesh->o_mesh)
		{
			continue;
		}
		if (!mesh->GetVertexBuffer())
		{
			continue;
		}
		renderItems.push_back(buildRenderItem(*mesh));
	}

	// --- Record and submit (reuse pre-allocated command buffer) ---
	vk::CommandBuffer cmdBufRaw = *r_commandBuffers[imageIndex];

	recordFrame(r_commandBuffers[imageIndex], imageIndex, *activeCam, renderItems, &scene);

	auto& renderFinished = r_renderFinishedSemaphores[imageIndex];
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo(*imageAvailable, waitStage, cmdBufRaw, *renderFinished);
	r_graphicsQueue.submit(submitInfo, *fence);

	// --- Present ---
	bool presentFailed = false;
	try
	{
		r_swapchain->Present(renderFinished, imageIndex, r_graphicsQueue);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Present failed: " << e.what());
		presentFailed = true;
	}

	// --- Handle swapchain recreation after present ---
	if (r_swapchain->generation() != r_swapchainGeneration)
	{
		recreateSwapchain();
		presentFailed = true;
	}

	if (presentFailed)
	{
		return;
	}

	r_currentFrame = (r_currentFrame + 1) % kMaxFramesInFlight;
}

void DeferredRenderer::WaitIdle()
{
	r_device.waitIdle();
}

void DeferredRenderer::HandleResize(uint32_t width, uint32_t height)
{
	uint32_t oldGen = r_swapchain->generation();
	r_swapchain->Recreate(width, height);
	if (r_swapchain->generation() != oldGen)
	{
		recreateSwapchain();
	}
}

// ---------------------------------------------------------------------------
// Swapchain recreation
// ---------------------------------------------------------------------------

void DeferredRenderer::recreateSwapchain()
{
	r_device.waitIdle();

	uint32_t newImageCount = r_swapchain->imageCount();
	vk::Extent2D newExtent = r_swapchain->extent();
	// Clear screen-space attachments (G-Buffer + shadow intensities).
	// Shadow cubemaps survive resize.  Attachments are re-created lazily
	// on first GetAttachment() call in the next frame.
	r_renderCache->CleanScreenSpace();

	// Rebuild render-finished semaphores (one per swapchain image)
	r_renderFinishedSemaphores.clear();
	for (uint32_t i = 0; i < newImageCount; ++i)
	{
		r_renderFinishedSemaphores.emplace_back(r_device, vk::SemaphoreCreateInfo());
	}

	// Rebuild command buffers if image count changed (swapchain image count
	// may differ across surfaces/drivers).
	if (r_commandBuffers.size() != newImageCount)
	{
		r_commandBuffers.clear();
		vk::CommandBufferAllocateInfo cmdBufAlloc(
			*r_commandPool, vk::CommandBufferLevel::ePrimary, newImageCount);
		r_commandBuffers = vk::raii::CommandBuffers(r_device, cmdBufAlloc);

#ifdef _DEBUG
		for (uint32_t i = 0; i < newImageCount; ++i)
		{
			char nameBuf[32];
			snprintf(nameBuf, sizeof(nameBuf), "DeferredRenderer::FrameCmd[%u]", i);
			const vk::DebugUtilsObjectNameInfoEXT nameInfo(
				vk::ObjectType::eCommandBuffer,
				reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*r_commandBuffers[i])),
				nameBuf);
			r_device.setDebugUtilsObjectNameEXT(nameInfo);
			NEURUS_LOG("[DeferredRenderer] CmdBuf[" << i << "] handle=0x"
			          << std::hex << reinterpret_cast<uint64_t>(
			                 static_cast<VkCommandBuffer>(*r_commandBuffers[i]))
			          << std::dec << " name='" << nameBuf << "'");
		}
#endif
	}

	r_swapchainGeneration = r_swapchain->generation();
}

// ---------------------------------------------------------------------------
// Frame recording
// ---------------------------------------------------------------------------

void DeferredRenderer::recordFrame(const vk::raii::CommandBuffer& cmdBuf, uint32_t imageIndex,
                                   const Camera& camera,
                                   const std::vector<GeometryRenderItem>& renderItems,
                                   const Scene* scene)
{
	const vk::Extent2D extent = r_swapchain->extent();

	// --- Reset command buffer (ensures it's not in a bad state) then begin ---
	cmdBuf.reset();
	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	cmdBuf.begin(beginInfo);

	// --- Compute camera matrices for this frame ---
	// --- Build per-frame render context (constructed once, passed to all passes) ---
	RenderContext ctx{};
	ctx.renderExtent = extent;
	ctx.frameIndex = r_currentFrame;
	ctx.view = camera.GetViewMatrix();
	ctx.viewProj = camera.GetProjectionMatrix() * ctx.view;
	ctx.cameraPos = camera.GetPosition();
	ctx.invProjView = glm::inverse(ctx.viewProj);
	ctx.renderItems = &renderItems;
	ctx.scene = scene;

	// --- Phase 1: GeometryPass → G-Buffer MRT (using caller-provided render items) ---
	r_geometryPass->Record(cmdBuf, *r_renderCache, ctx);

	// --- Phase 1b: ShadowDepthPass → cubemap depth from light's POV ---
	r_shadowDepthPass->Record(cmdBuf, *r_renderCache, ctx);

	// --- Phase 1c: ShadowIntensityPass → per-pixel shadow evaluation from cubemap ---
	r_shadowIntensityPass->Record(cmdBuf, *r_renderCache, ctx);

	// --- Phase 2: SSAO → compute ambient occlusion from G-Buffer ---
	r_ssaoPass->Record(cmdBuf, *r_renderCache, ctx);

	// --- Phase 3: LightingPass → compute PBR → HDRColor ---
	r_lightingPass->Record(cmdBuf, *r_renderCache, ctx);

	// --- Phase 4: Blit HDRColor → swapchain image ---
	auto& hdrColor = r_renderCache->GetAttachment(AttachmentName::HDRColor, extent);
	const vk::Image hdrImage = *hdrColor.ImageHandle();
	const vk::Image swapchainImage = r_swapchain->images()[imageIndex];

	// Barrier 1: HDRColor GENERAL → TRANSFER_SRC_OPTIMAL
	// Barrier 2: Swapchain image UNDEFINED → TRANSFER_DST_OPTIMAL
	{
		Barrier::Transition(*cmdBuf, hdrColor, ImageState::TransferSrc);

		const auto scBarrier = vk::ImageMemoryBarrier2(
			vk::PipelineStageFlagBits2::eTopOfPipe,
			vk::AccessFlagBits2::eNone,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			swapchainImage,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

		const vk::DependencyInfo depInfo({}, {}, {}, scBarrier);
		cmdBuf.pipelineBarrier2(depInfo);
	}

	// --- Blit: HDRColor → swapchain image ---
	{
		vk::ImageBlit blitRegion;
		blitRegion.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		blitRegion.srcOffsets[0] = vk::Offset3D(0, 0, 0);
		blitRegion.srcOffsets[1] = vk::Offset3D(static_cast<int32_t>(extent.width),
		                                         static_cast<int32_t>(extent.height), 1);
		blitRegion.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		blitRegion.dstOffsets[0] = vk::Offset3D(0, 0, 0);
		blitRegion.dstOffsets[1] = vk::Offset3D(static_cast<int32_t>(extent.width),
		                                         static_cast<int32_t>(extent.height), 1);

		cmdBuf.blitImage(hdrImage,
		                 vk::ImageLayout::eTransferSrcOptimal,
		                 swapchainImage,
		                 vk::ImageLayout::eTransferDstOptimal,
		                 blitRegion,
		                 vk::Filter::eLinear);
	}

	// --- Transition swapchain image to PRESENT_SRC_KHR ---
	{
		const auto barrier = vk::ImageMemoryBarrier2(
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite,
			vk::PipelineStageFlagBits2::eBottomOfPipe,
			vk::AccessFlagBits2::eNone,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			swapchainImage,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

		const vk::DependencyInfo depInfo({}, {}, {}, barrier);
		cmdBuf.pipelineBarrier2(depInfo);
	}

	// --- End command buffer ---
	cmdBuf.end();
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

bool DeferredRenderer::TakeScreenshot()
{
	if (!r_swapchain)
	{
		return false;
	}

	const auto& images = r_swapchain->images();
	if (r_lastImageIndex >= images.size())
	{
		return false;
	}

	const vk::Image scImage = images[r_lastImageIndex];
	const vk::Format scFormat = r_swapchain->format();
	const vk::Extent2D scExtent = r_swapchain->extent();

	const std::string path = Screenshot::timestampedFilename("screenshots/swapchain", ".png");

	return Screenshot::CaptureSwapchain(r_device, r_physicalDevice,
	                                     r_graphicsQueue, r_queueFamilyIndex,
	                                     scImage, scFormat, scExtent, path);
}

int DeferredRenderer::TakeScreenshotAllAttachments()
{
	int count = 0;

	if (r_renderCache)
	{
		count = Screenshot::CaptureAllAttachments(r_device, r_physicalDevice,
		                                          r_graphicsQueue, r_queueFamilyIndex,
		                                          *r_renderCache,
		                                          r_swapchain->extent(),
		                                          "screenshots/gbuffer");
	}

	// --- Export shadow cubemaps as equirectangular PNGs ---
	{
		const auto shadowUIDs = r_renderCache->GetShadowMapUIDs();
		for (int lightUID : shadowUIDs)
		{
			const std::string result = ExportShadowDepthEquirect(
				lightUID, "screenshots/shadow_cubemap");
			if (!result.empty())
			{
				++count;
			}
		}
	}

	// --- Export shadow intensity array layers ---
	{
		Image* intensityArray = r_renderCache->GetShadowIntensityArray();
		if (intensityArray)
		{
			const vk::Extent2D extent = r_swapchain->extent();
			const auto shadowUIDs = r_renderCache->GetShadowMapUIDs();
			for (int lightUID : shadowUIDs)
			{
				const uint32_t layer = r_renderCache->GetShadowIntensityLayerIndex(lightUID);
				const std::string path = Screenshot::timestampedFilename(
					"screenshots/shadow_intensity_Light" + std::to_string(lightUID), ".png");
				if (Screenshot::CaptureImageLayer(r_device, r_physicalDevice,
				                                   r_graphicsQueue, r_queueFamilyIndex,
				                                   *intensityArray, layer, path))
				{
					++count;
				}
			}
		}
	}

	return count;
}

// ===========================================================================
// C2E — Shadow cubemap → Equirectangular export
// ===========================================================================

std::string DeferredRenderer::ExportShadowDepthEquirect(const int lightUID,
                                                          const std::string& filenamePrefix)
{
	if (!r_shadowDepthPass || !r_renderCache)
	{
		return {};
	}

	auto& cubemap = r_renderCache->GetShadowMap(lightUID, LightType::POINTLIGHT);
	const uint32_t cubeRes = r_shadowDepthPass->Resolution();
	const uint32_t equiWidth = cubeRes * 2;
	const uint32_t equiHeight = cubeRes;

	// --- 1. Create temporary equirect output image (rgba32f) ---
	Image equirectImage(r_device, r_physicalDevice,
	                    vk::Extent2D{equiWidth, equiHeight},
	                    vk::Format::eR32G32B32A32Sfloat,
	                    vk::ImageUsageFlagBits::eStorage |
	                        vk::ImageUsageFlagBits::eTransferSrc,
	                    1u, Image::ImageType::e2D,
	                    "ShadowEquirectTemp");

	// --- 2. Create sampler for depth cubemap ---
	vk::SamplerCreateInfo samplerCI(
		{}, vk::Filter::eNearest, vk::Filter::eNearest,
		vk::SamplerMipmapMode::eNearest,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		0.0f, VK_FALSE, 0.0f, VK_FALSE,
		vk::CompareOp::eAlways,
		0.0f, 0.0f, vk::BorderColor::eFloatTransparentBlack, VK_FALSE);
	vk::raii::Sampler cubeSampler(r_device, samplerCI);

	// --- 3. Descriptor set layout (2 bindings) ---
	DescriptorSetLayout c2eLayout = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1, vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(r_device);

	DescriptorPool c2ePool(r_device, 1,
		DescriptorPool::CalculatePoolSizes({&c2eLayout}, 1));
	auto c2eSet = std::move(c2ePool.Allocate(c2eLayout, 1).front());

	// Write descriptors
	{
		vk::DescriptorImageInfo cubeInfo(cubeSampler, *cubemap.ImageViewHandle(),
		                                  vk::ImageLayout::eShaderReadOnlyOptimal);
		c2eSet.WriteImage(0, cubeInfo, vk::DescriptorType::eCombinedImageSampler);

		vk::DescriptorImageInfo equiInfo(nullptr, *equirectImage.ImageViewHandle(),
		                                  vk::ImageLayout::eGeneral);
		c2eSet.WriteImage(1, equiInfo, vk::DescriptorType::eStorageImage);
	}

	// --- 4. Create compute pipeline ---
	auto compModule = ShaderModule::FromEmbedded(r_device,
		c2e_comp_spv, sizeof(c2e_comp_spv));

	ComputePipelineBuilder c2eBuilder(r_device);
	c2eBuilder.SetShaderStage(std::move(compModule), "main");
	c2eBuilder.SetDebugName("DeferredRenderer::CubemapToEquirect");
	c2eBuilder.AddDescriptorSetLayout(*c2eLayout.layout());

	auto c2ePipeline = c2eBuilder.BuildComputePipeline();
	vk::PipelineLayout c2ePipelineLayout = *c2eBuilder.pipelineLayout();

	// --- 5. Record & dispatch ---
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                  r_queueFamilyIndex);
		vk::raii::CommandPool cmdPool(r_device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(r_device, allocInfo);

		auto& cmd = cmdBufs[0];
		cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		// Transition cubemap → SHADER_READ_ONLY
		{
			Barrier::Transition(*cmd, cubemap, ImageState::ColorShaderRead);
		}

		// Transition equirect → GENERAL
		{
			Barrier::Transition(*cmd, equirectImage, ImageState::ShaderWrite);
		}

		// Bind and dispatch
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *c2ePipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
		                       c2ePipelineLayout, 0,
		                       {c2eSet.handle()}, {});

		uint32_t gx = (equiWidth  + 15) / 16;
		uint32_t gy = (equiHeight + 15) / 16;
		cmd.dispatch(gx, gy, 1);

		// Transition equirect → TRANSFER_SRC for readback
		{
			Barrier::Transition(*cmd, equirectImage, ImageState::TransferSrc);
		}

		cmd.end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmd));
		r_graphicsQueue.submit(submitInfo);
		r_graphicsQueue.waitIdle();
	}

	// --- 6. Read back equirect as grayscale PNG ---
	auto equirectData = equirectImage.ReadImageData(
		r_device, r_physicalDevice, r_graphicsQueue, r_queueFamilyIndex);

	if (!equirectData.IsValid())
	{
		NEURUS_ERR("[ExportShadowDepthEquirect] Readback failed for lightUID=" << lightUID);
		return {};
	}

	const auto& rawPixelData = equirectData.GetPixelData();
	const size_t pixelCount = static_cast<size_t>(equiWidth) * equiHeight;
	std::vector<uint8_t> grayPixels(pixelCount);

	for (size_t i = 0; i < pixelCount; ++i)
	{
		float r;
		std::memcpy(&r, &rawPixelData[i * 16], sizeof(float));
		r = std::max(0.0f, std::min(1.0f, r));
		grayPixels[i] = static_cast<uint8_t>(r * 255.0f + 0.5f);
	}

	const std::string path = Screenshot::timestampedFilename(
		filenamePrefix + "_Light" + std::to_string(lightUID), ".png");

	ImageData grayImg(grayPixels.data(), equiWidth, equiHeight, vk::Format::eR8Unorm);
	const bool saved = grayImg.SavePNG(path);

	if (saved)
	{
		NEURUS_LOG("[ExportShadowDepthEquirect] Saved " << path
		           << " (" << equiWidth << "x" << equiHeight << ")");
		return path;
	}
	return {};
}

} // namespace neurus
