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

// Generated SPIR-V shader headers
#include "gbuffer.vert.h"
#include "gbuffer.frag.h"
#include "pbr_lighting.comp.h"
#include "ssao.comp.h"
#include "irradiance_conv.comp.h"
#include "importance_samp.comp.h"
#include "shadow_depth.vert.h"
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
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
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
	: m_device(device)
	, m_physicalDevice(physicalDevice)
	, m_graphicsQueue(graphicsQueue)
	, m_queueFamilyIndex(queueFamilyIndex)
	, m_commandPool(createCommandPool(device, queueFamilyIndex))
{
	// --- 1. Create swapchain ---
	m_swapchain = std::make_unique<Swapchain>(physicalDevice, device, surface, width, height);
	const auto extent = m_swapchain->extent();

	// --- 2. Create G-Buffer attachment cache (lazy creation on first access) ---
	m_renderCache = std::make_unique<RenderCache>(device, physicalDevice);

	// --- 3. Create geometry pass ---
	{
		auto geoPass = std::make_unique<GeometryPass>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));
		m_geometryPass = geoPass.get();
		m_passes.push_back(std::move(geoPass));
	}

	// --- 5. Create lighting pass ---
	{
		auto lightPass = std::make_unique<LightingPass>(
			device, physicalDevice,
			kMaxFramesInFlight,
			m_graphicsQueue, m_queueFamilyIndex,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
		m_lightingPass = lightPass.get();
		m_passes.push_back(std::move(lightPass));
	}

	// --- 6. Create SSAO pass ---
	{
		auto ssaoPass = std::make_unique<SSAOPass>(
			device, physicalDevice,
			kMaxFramesInFlight,
			m_graphicsQueue, m_queueFamilyIndex,
			ssao_comp_spv, sizeof(ssao_comp_spv));
		m_ssaoPass = ssaoPass.get();
		m_passes.push_back(std::move(ssaoPass));
	}

	// --- 7. Create IBL pass (pure compute service) ---
	{
		auto iblPass = std::make_unique<IBLPass>(
			device, physicalDevice,
			m_graphicsQueue, m_queueFamilyIndex,
			irradiance_conv_comp_spv, sizeof(irradiance_conv_comp_spv),
			importance_samp_comp_spv, sizeof(importance_samp_comp_spv));
		m_iblPass = iblPass.get();
		m_passes.push_back(std::move(iblPass));
		NEURUS_LOG("[DeferredRenderer] IBLPass created");
	}

	// --- 8. Create shadow depth pass (cubemap depth from light's POV) ---
	{
		auto shadowDepth = std::make_unique<ShadowDepthPass>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			ShadowDepthPass::kDefaultResolution);
		m_shadowDepthPass = shadowDepth.get();
		m_passes.push_back(std::move(shadowDepth));
		NEURUS_LOG("[DeferredRenderer] ShadowDepthPass created");
	}

	// --- 8b. Create shadow intensity pass (per-pixel shadow evaluation from cubemap) ---
	{
		auto shadowIntensity = std::make_unique<ShadowIntensityPass>(
			device, physicalDevice,
			kMaxFramesInFlight,
			graphicsQueue, queueFamilyIndex,
			shadow_eval_comp_spv, sizeof(shadow_eval_comp_spv));
		m_shadowIntensityPass = shadowIntensity.get();
		m_passes.push_back(std::move(shadowIntensity));
		NEURUS_LOG("[DeferredRenderer] ShadowIntensityPass created");
	}

	// --- 9. Allocate command buffers (one per swapchain image, reused) ---
	uint32_t imageCount = m_swapchain->imageCount();

	// Verify the swapchain supports TRANSFER_DST for the blit composite path
	const bool hasTransferDst = (m_swapchain->actualImageUsage() & vk::ImageUsageFlagBits::eTransferDst) != vk::ImageUsageFlags{};
	if (!hasTransferDst)
	{
		throw std::runtime_error(
			"Swapchain surface does not support VK_IMAGE_USAGE_TRANSFER_DST_BIT.\n"
			"DeferredRenderer requires TRANSFER_DST for the HDRColor-to-swapchain blit composite.\n"
			"Try running on a GPU/driver that supports this usage flag for the surface format.");
	}

	vk::CommandBufferAllocateInfo cmdBufAlloc(*m_commandPool, vk::CommandBufferLevel::ePrimary, imageCount);
	m_commandBuffers = vk::raii::CommandBuffers(device, cmdBufAlloc);

	// --- Set debug names for command buffers ---
#ifdef _DEBUG
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		char nameBuf[32];
		snprintf(nameBuf, sizeof(nameBuf), "DeferredRenderer::FrameCmd[%u]", i);
		const vk::DebugUtilsObjectNameInfoEXT nameInfo(
			vk::ObjectType::eCommandBuffer,
			reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*m_commandBuffers[i])),
			nameBuf);
		device.setDebugUtilsObjectNameEXT(nameInfo);
		NEURUS_LOG("[DeferredRenderer] CmdBuf[" << i << "] handle=0x"
		          << std::hex << reinterpret_cast<uint64_t>(
		                 static_cast<VkCommandBuffer>(*m_commandBuffers[i]))
		          << std::dec << " name='" << nameBuf << "'");
	}
#endif

	// --- 10. Create sync objects ---
	for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
	{
		m_inFlightFences.emplace_back(device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
		m_imageAvailableSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
	}
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		m_renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
	}

	NEURUS_LOG("[DeferredRenderer] " << extent.width << "x" << extent.height
	          << " swapchainImages=" << imageCount
	          << " transferDst=" << (hasTransferDst ? "yes" : "no"));
}

DeferredRenderer::~DeferredRenderer()
{
	WaitIdle();
	// vk::raii destructors clean up automatically in reverse declaration order.
	// m_passes vector destroys all passes in construction order (GeometryPass → LightingPass →
	//   SSAOPass → IBLPass), before RenderCache.
}

// ---------------------------------------------------------------------------
// Light upload delegation
// ---------------------------------------------------------------------------

void DeferredRenderer::UploadLights(const Scene& scene)
{
	if (m_lightingPass)
	{
		m_lightingPass->UploadLights(scene);
	}

	// --- Configure shadow depth pass from the first point light with use_shadow ---
	m_activeShadowLightUID = -1;
	if (m_shadowDepthPass)
	{
		for (const auto& [id, light] : scene.light_list)
		{
			if (!light) continue;
			if (light->light_type == LightType::POINTLIGHT && light->use_shadow)
			{
				m_activeShadowLightUID = id;
				break;  // Only first shadow-casting point light for MVP
			}
		}
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
// Camera data computation
// ---------------------------------------------------------------------------

CameraUBOData DeferredRenderer::computeCameraData(vk::Extent2D extent, const Camera& camera) const
{
	const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

	const glm::mat4 view = camera.GetViewMatrix();
	const glm::mat4 proj = glm::perspective(glm::radians(camera.cam_pers), aspect, camera.cam_near, camera.cam_far);

	CameraUBOData data;
	data.viewProj = proj * view;
	data.view = view;
	return data;
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
	m_iblPass->Generate(equirectImage, diffuseOut, specularOut);
}

// ---------------------------------------------------------------------------
// DrawFrame - main render loop entry point
// ---------------------------------------------------------------------------

void DeferredRenderer::DrawFrame()
{
	// Fallback camera matching the old hardcoded defaults.
	Camera fallbackCam;
	fallbackCam.SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	fallbackCam.cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);

	auto& fence = m_inFlightFences[m_currentFrame];
	auto& imageAvailable = m_imageAvailableSemaphores[m_currentFrame];

	// --- Wait for this frame slot's fence ---
	if (m_device.waitForFences(*fence, VK_TRUE, kFenceTimeoutNs) != vk::Result::eSuccess)
	{
		return;  // Timeout - skip this frame
	}

	// --- Acquire next swapchain image ---
	uint32_t imageIndex = 0;
	bool skipFrame = false;
	try
	{
		imageIndex = m_swapchain->AcquireNextImage(imageAvailable);
		m_lastImageIndex = imageIndex;
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
	if (m_swapchain->generation() != m_swapchainGeneration)
	{
		recreateSwapchain();
		skipFrame = true;
	}

	if (skipFrame)
	{
		// Don't advance m_currentFrame - retry same slot next frame
		return;
	}

	// Only reset fence after successful acquire
	m_device.resetFences(*fence);

	// --- Record and submit (reuse pre-allocated command buffer) ---
	vk::CommandBuffer cmdBufRaw = *m_commandBuffers[imageIndex];

	// No-args DrawFrame is deprecated and used only as camera-fallback.
	// Pass empty render items (no geometry drawn) - the fallback exists only
	// to prevent a crash when no camera is configured.
	recordFrame(m_commandBuffers[imageIndex], imageIndex, fallbackCam, {}, nullptr);

	auto& renderFinished = m_renderFinishedSemaphores[imageIndex];
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo(*imageAvailable, waitStage, cmdBufRaw, *renderFinished);
	m_graphicsQueue.submit(submitInfo, *fence);

	// --- Present ---
	bool presentFailed = false;
	try
	{
		m_swapchain->Present(renderFinished, imageIndex, m_graphicsQueue);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Present failed: " << e.what());
		presentFailed = true;
	}

	// --- Handle swapchain recreation after present ---
	if (m_swapchain->generation() != m_swapchainGeneration)
	{
		recreateSwapchain();
		presentFailed = true;
	}

	if (presentFailed)
	{
		// Don't advance frame - retry same slot next iteration
		return;
	}

	m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
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

	auto& fence = m_inFlightFences[m_currentFrame];
	auto& imageAvailable = m_imageAvailableSemaphores[m_currentFrame];

	// --- Wait for this frame slot's fence ---
	if (m_device.waitForFences(*fence, VK_TRUE, kFenceTimeoutNs) != vk::Result::eSuccess)
	{
		return;  // Timeout - skip this frame
	}

	// --- Acquire next swapchain image ---
	uint32_t imageIndex = 0;
	bool skipFrame = false;
	try
	{
		imageIndex = m_swapchain->AcquireNextImage(imageAvailable);
		m_lastImageIndex = imageIndex;
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
	if (m_swapchain->generation() != m_swapchainGeneration)
	{
		recreateSwapchain();
		skipFrame = true;
	}

	if (skipFrame)
	{
		return;
	}

	// Only reset fence after successful acquire
	m_device.resetFences(*fence);

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
	vk::CommandBuffer cmdBufRaw = *m_commandBuffers[imageIndex];

	recordFrame(m_commandBuffers[imageIndex], imageIndex, *activeCam, renderItems, &scene);

	auto& renderFinished = m_renderFinishedSemaphores[imageIndex];
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo(*imageAvailable, waitStage, cmdBufRaw, *renderFinished);
	m_graphicsQueue.submit(submitInfo, *fence);

	// --- Present ---
	bool presentFailed = false;
	try
	{
		m_swapchain->Present(renderFinished, imageIndex, m_graphicsQueue);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Present failed: " << e.what());
		presentFailed = true;
	}

	// --- Handle swapchain recreation after present ---
	if (m_swapchain->generation() != m_swapchainGeneration)
	{
		recreateSwapchain();
		presentFailed = true;
	}

	if (presentFailed)
	{
		return;
	}

	m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

void DeferredRenderer::WaitIdle()
{
	m_device.waitIdle();
}

void DeferredRenderer::HandleResize(uint32_t width, uint32_t height)
{
	uint32_t oldGen = m_swapchain->generation();
	m_swapchain->Recreate(width, height);
	if (m_swapchain->generation() != oldGen)
	{
		recreateSwapchain();
	}
}

// ---------------------------------------------------------------------------
// Swapchain recreation
// ---------------------------------------------------------------------------

void DeferredRenderer::recreateSwapchain()
{
	m_device.waitIdle();

	uint32_t newImageCount = m_swapchain->imageCount();
	vk::Extent2D newExtent = m_swapchain->extent();
	// Clear screen-space attachments (G-Buffer + shadow intensities).
	// Shadow cubemaps survive resize.  Attachments are re-created lazily
	// on first GetAttachment() call in the next frame.
	m_renderCache->CleanScreenSpace();

	// Rebuild render-finished semaphores (one per swapchain image)
	m_renderFinishedSemaphores.clear();
	for (uint32_t i = 0; i < newImageCount; ++i)
	{
		m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
	}

	// Rebuild command buffers if image count changed (swapchain image count
	// may differ across surfaces/drivers).
	if (m_commandBuffers.size() != newImageCount)
	{
		m_commandBuffers.clear();
		vk::CommandBufferAllocateInfo cmdBufAlloc(
			*m_commandPool, vk::CommandBufferLevel::ePrimary, newImageCount);
		m_commandBuffers = vk::raii::CommandBuffers(m_device, cmdBufAlloc);

#ifdef _DEBUG
		for (uint32_t i = 0; i < newImageCount; ++i)
		{
			char nameBuf[32];
			snprintf(nameBuf, sizeof(nameBuf), "DeferredRenderer::FrameCmd[%u]", i);
			const vk::DebugUtilsObjectNameInfoEXT nameInfo(
				vk::ObjectType::eCommandBuffer,
				reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(*m_commandBuffers[i])),
				nameBuf);
			m_device.setDebugUtilsObjectNameEXT(nameInfo);
			NEURUS_LOG("[DeferredRenderer] CmdBuf[" << i << "] handle=0x"
			          << std::hex << reinterpret_cast<uint64_t>(
			                 static_cast<VkCommandBuffer>(*m_commandBuffers[i]))
			          << std::dec << " name='" << nameBuf << "'");
		}
#endif
	}

	m_swapchainGeneration = m_swapchain->generation();
}

// ---------------------------------------------------------------------------
// Frame recording
// ---------------------------------------------------------------------------

void DeferredRenderer::recordFrame(const vk::raii::CommandBuffer& cmdBuf, uint32_t imageIndex,
                                   const Camera& camera,
                                   const std::vector<GeometryRenderItem>& renderItems,
                                   const Scene* scene)
{
	const vk::Extent2D extent = m_swapchain->extent();

	// --- Begin command buffer ---
	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	cmdBuf.begin(beginInfo);

	// --- Compute camera matrices for this frame ---
	// --- Build per-frame render context (constructed once, passed to all passes) ---
	const CameraUBOData cameraData = computeCameraData(extent, camera);
	RenderContext ctx{};
	ctx.renderExtent = extent;
	ctx.frameIndex = m_currentFrame;
	ctx.viewProj = cameraData.viewProj;
	ctx.view = cameraData.view;
	ctx.cameraPos = camera.GetPosition();
	ctx.invProjView = glm::inverse(cameraData.viewProj);
	ctx.renderItems = &renderItems;
	ctx.scene = scene;
	ctx.lightUID = m_activeShadowLightUID;

	// --- Phase 1: GeometryPass → G-Buffer MRT (using caller-provided render items) ---
	m_geometryPass->Record(cmdBuf, *m_renderCache, ctx);

	// --- Phase 1b: ShadowDepthPass → cubemap depth from light's POV ---
	m_shadowDepthPass->Record(cmdBuf, *m_renderCache, ctx);

	// --- Phase 1c: ShadowIntensityPass → per-pixel shadow evaluation from cubemap ---
	m_shadowIntensityPass->Record(cmdBuf, *m_renderCache, ctx);

	// --- Phase 2: SSAO → compute ambient occlusion from G-Buffer ---
	m_ssaoPass->Record(cmdBuf, *m_renderCache, ctx);

	// --- Phase 3: LightingPass → compute PBR → HDRColor ---
	m_lightingPass->Record(cmdBuf, *m_renderCache, ctx);

	// --- Phase 4: Blit HDRColor → swapchain image ---
	auto& hdrColor = m_renderCache->GetAttachment(AttachmentName::HDRColor, extent);
	const vk::Image hdrImage = *hdrColor.ImageHandle();
	const vk::Image swapchainImage = m_swapchain->images()[imageIndex];

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
	if (!m_swapchain)
	{
		return false;
	}

	const auto& images = m_swapchain->images();
	if (m_lastImageIndex >= images.size())
	{
		return false;
	}

	const vk::Image scImage = images[m_lastImageIndex];
	const vk::Format scFormat = m_swapchain->format();
	const vk::Extent2D scExtent = m_swapchain->extent();

	const std::string path = Screenshot::timestampedFilename("screenshots/swapchain", ".png");

	return Screenshot::CaptureSwapchain(m_device, m_physicalDevice,
	                                     m_graphicsQueue, m_queueFamilyIndex,
	                                     scImage, scFormat, scExtent, path);
}

int DeferredRenderer::TakeScreenshotAllAttachments()
{
	int count = 0;

	if (m_renderCache)
	{
		count = Screenshot::CaptureAllAttachments(m_device, m_physicalDevice,
		                                          m_graphicsQueue, m_queueFamilyIndex,
		                                          *m_renderCache,
		                                          m_swapchain->extent(),
		                                          "screenshots/gbuffer");
	}

	// Also export shadow cubemap as equirectangular PNG
	if (m_shadowDepthPass)
	{
		ExportShadowDepthEquirect("screenshots/shadow_depth_point_0_equirect");
	}

	return count;
}

// ===========================================================================
// C2E — Shadow cubemap → Equirectangular export
// ===========================================================================

std::string DeferredRenderer::ExportShadowDepthEquirect(const std::string& filenamePrefix)
{
	if (!m_shadowDepthPass || m_activeShadowLightUID < 0)
	{
		return {};
	}

	auto& cubemap = m_renderCache->GetShadowMap(m_activeShadowLightUID);
	const uint32_t cubeRes = m_shadowDepthPass->Resolution();
	const uint32_t equiWidth = cubeRes * 2;
	const uint32_t equiHeight = cubeRes;

	// --- 1. Create temporary equirect output image (rgba32f) ---
	Image equirectImage(m_device, m_physicalDevice,
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
	vk::raii::Sampler cubeSampler(m_device, samplerCI);

	// --- 3. Descriptor set layout (2 bindings) ---
	DescriptorSetLayout c2eLayout = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1, vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(m_device);

	DescriptorPool c2ePool(m_device, 1,
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
	auto compModule = ShaderModule::FromEmbedded(m_device,
		c2e_comp_spv, sizeof(c2e_comp_spv));

	ComputePipelineBuilder c2eBuilder(m_device);
	c2eBuilder.SetShaderStage(std::move(compModule), "main");
	c2eBuilder.AddDescriptorSetLayout(*c2eLayout.layout());

	auto c2ePipeline = c2eBuilder.BuildComputePipeline();
	vk::PipelineLayout c2ePipelineLayout = *c2eBuilder.pipelineLayout();

	// --- 5. Record & dispatch ---
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                  m_queueFamilyIndex);
		vk::raii::CommandPool cmdPool(m_device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(m_device, allocInfo);

		auto& cmd = cmdBufs[0];
		cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		// Transition cubemap → SHADER_READ_ONLY (if not already)
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
		m_graphicsQueue.submit(submitInfo);
		m_graphicsQueue.waitIdle();
	}

	// --- 6. Read back equirect as grayscale PNG ---
	//     The C2E output is rgba32f; we read only the R channel (depth value).
	auto equirectData = equirectImage.ReadImageData(
		m_device, m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex);

	if (!equirectData.IsValid())
	{
		NEURUS_ERR("[ExportShadowDepthEquirect] Readback failed");
		return {};
	}

	const auto& rawPixelData = equirectData.GetPixelData();
	// Convert rgba32f → grayscale uint8
	const size_t pixelCount = static_cast<size_t>(equiWidth) * equiHeight;
	std::vector<uint8_t> grayPixels(pixelCount);

	for (size_t i = 0; i < pixelCount; ++i)
	{
		// Each pixel is 4 × float = 16 bytes; the R channel is at byte offset 0
		float r;
		std::memcpy(&r, &rawPixelData[i * 16], sizeof(float));
		// Clamp to [0, 1] and convert to uint8
		r = std::max(0.0f, std::min(1.0f, r));
		grayPixels[i] = static_cast<uint8_t>(r * 255.0f + 0.5f);
	}

	const std::string path = Screenshot::timestampedFilename(filenamePrefix, ".png");

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
