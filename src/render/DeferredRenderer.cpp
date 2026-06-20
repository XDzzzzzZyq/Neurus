/**
 * @file DeferredRenderer.cpp
 * @brief Deferred rendering pipeline implementation.
 */

#include "DeferredRenderer.h"

#include "passes/AttachmentManager.h"
#include "passes/GeometryPass.h"
#include "passes/LightingPass.h"
#include "passes/RenderPassManager.h"
#include "Image.h"
#include "Screenshot.h"
#include "passes/SSAOPass.h"
#include "Swapchain.h"
#include "passes/SyncObjects.h"
#include "VulkanBuffer.h"
#include "buffers/VertexBuffer.h"
#include "buffers/IndexBuffer.h"

// Generated SPIR-V shader headers
#include "gbuffer.vert.h"
#include "gbuffer.frag.h"
#include "pbr_lighting.comp.h"
#include "ssao.comp.h"
#include "irradiance_conv.comp.h"
#include "importance_samp.comp.h"

#include <glm/gtc/matrix_transform.hpp>

#include "Log.h"

#include "scene/Camera.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <stb_image.h>

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
	, m_surface(surface)
	, m_width(width)
	, m_height(height)
	, m_commandPool(createCommandPool(device, queueFamilyIndex))
{
	// --- 1. Create swapchain ---
	m_swapchain = std::make_unique<Swapchain>(physicalDevice, device, surface, width, height);
	const auto extent = m_swapchain->extent();
	m_width = extent.width;
	m_height = extent.height;

	// --- 2. Create G-Buffer attachments ---
	m_attachmentManager = std::make_unique<AttachmentManager>(device, physicalDevice);
	m_attachmentManager->Create(extent);

	// --- 3. Create render pass manager ---
	m_renderPassManager = std::make_unique<RenderPassManager>();

	// --- 4. Create fallback SSBO for zero-light scenes ---
	//     LightingPass::Record needs a valid VulkanBuffer reference even when
	//     no lights are present (the SSBO descriptor must be bound, even
	//     though the shader won't read it when lightCount=0).
	{
		uint8_t zero[sizeof(PointLightGpu)] = {};
		m_fallbackSSBO = std::make_unique<VulkanBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			sizeof(PointLightGpu),
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal);
		m_fallbackSSBO->Upload(zero, sizeof(PointLightGpu));
		NEURUS_LOG("[DeferredRenderer] Created fallback SSBO (zero-light)");
	}

	// --- 5. Create geometry pass ---
	m_geometryPass = std::make_unique<GeometryPass>(
		device, physicalDevice, graphicsQueue, queueFamilyIndex,
		*m_attachmentManager, *m_renderPassManager,
		gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
		gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

	// --- 7. Create lighting pass ---
	m_lightingPass = std::make_unique<LightingPass>(
		device, physicalDevice,
		*m_attachmentManager,
		kMaxFramesInFlight,
		m_graphicsQueue, m_queueFamilyIndex,
		pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));

	// --- 7b. Create SSAO pass ---
	m_ssaoPass = std::make_unique<SSAOPass>(
		device, physicalDevice,
		*m_attachmentManager,
		kMaxFramesInFlight,
		m_graphicsQueue, m_queueFamilyIndex,
		ssao_comp_spv, sizeof(ssao_comp_spv));

	// --- 7c. Create IBL pass (pure compute service) ---
	{
		m_iblPass = std::make_unique<IBLPass>(
			device, physicalDevice,
			m_graphicsQueue, m_queueFamilyIndex,
			irradiance_conv_comp_spv, sizeof(irradiance_conv_comp_spv),
			importance_samp_comp_spv, sizeof(importance_samp_comp_spv));
		NEURUS_LOG("[DeferredRenderer] IBLPass created");
	}

	// --- 7d. Create IBL cubemap Images (owned by DeferredRenderer) ---
	{
		const vk::ImageUsageFlags cubeUsage =
			vk::ImageUsageFlagBits::eStorage      // compute write
			| vk::ImageUsageFlagBits::eSampled    // shader read
			| vk::ImageUsageFlagBits::eTransferSrc; // readback for saving

		m_diffuseCubemap = std::make_unique<Image>(
			device, physicalDevice,
			vk::Extent2D{IBLPass::kDiffuseFaceRes, IBLPass::kDiffuseFaceRes},
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage,
			/*mipLevels=*/1,
			Image::ImageType::eCube,
			"IBL_DiffuseCubemap");

		m_specularCubemap = std::make_unique<Image>(
			device, physicalDevice,
			vk::Extent2D{IBLPass::kSpecularFaceRes, IBLPass::kSpecularFaceRes},
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage,
			/*mipLevels=*/IBLPass::kSpecularMipLevels,
			Image::ImageType::eCube,
			"IBL_SpecularCubemap");

		// Create cubemap samplers
		m_diffuseSampler = IBLPass::CreateCubemapSampler(device, 1);
		m_specularSampler = IBLPass::CreateCubemapSampler(device, IBLPass::kSpecularMipLevels);

		NEURUS_LOG("[DeferredRenderer] Created IBL cubemaps: diffuse "
		           << IBLPass::kDiffuseFaceRes << "², specular "
		           << IBLPass::kSpecularFaceRes << "² x "
		           << IBLPass::kSpecularMipLevels << " mips");
	}

	// --- 8. Create command pool ---
	// (initialized in member initializer list via createCommandPool)

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

	NEURUS_LOG("[DeferredRenderer] " << m_width << "x" << m_height
	          << " swapchainImages=" << imageCount
	          << " transferDst=" << (hasTransferDst ? "yes" : "no"));
}

DeferredRenderer::~DeferredRenderer()
{
	WaitIdle();
	// vk::raii destructors clean up automatically in reverse declaration order.
	// Unique_ptr destruction order: m_lightingPass → m_geometryPass → m_renderPassManager →
	//   m_attachmentManager → m_swapchain → ... (reverse member declaration).
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
}

// ---------------------------------------------------------------------------
// IBL wiring
// ---------------------------------------------------------------------------

void DeferredRenderer::EnableIBL()
{
	if (!m_iblPass || !m_lightingPass || !m_diffuseCubemap || !m_specularCubemap)
	{
		return;
	}

	m_lightingPass->SetIBLResources(
		*m_diffuseCubemap->ImageViewHandle(),
		*m_diffuseSampler,
		*m_specularCubemap->ImageViewHandle(),
		*m_specularSampler);

	NEURUS_LOG("[DeferredRenderer] IBL enabled in lighting pass");
}

void DeferredRenderer::SetEquirectEnvironment(const Image& equirect)
{
	if (!m_iblPass || !m_diffuseCubemap || !m_specularCubemap)
	{
		NEURUS_ERR("[DeferredRenderer] SetEquirectEnvironment: IBLPass or cubemaps not created");
		return;
	}

	m_iblPass->Generate(equirect, *m_diffuseCubemap, *m_specularCubemap);
	EnableIBL();
	NEURUS_LOG("[DeferredRenderer] IBL environment set and enabled");
}

void DeferredRenderer::OnEnvironmentChanged(const EnvironmentChanged& e)
{
	NEURUS_LOG("[DeferredRenderer] OnEnvironmentChanged (sceneId="
	           << e.sceneId << ", envId=" << e.envId
	           << "), regenerating IBL cubemaps");

	if (!m_scene)
	{
		NEURUS_ERR("[DeferredRenderer] OnEnvironmentChanged: no scene set (call SetScene first)");
		return;
	}

	// Look up Environment by envId from scene.env_list
	auto it = m_scene->env_list.find(e.envId);
	if (it == m_scene->env_list.end())
	{
		NEURUS_ERR("[DeferredRenderer] OnEnvironmentChanged: env ID "
		           << e.envId << " not found in scene");
		return;
	}

	const auto& env = it->second;
	const std::string& hdrPath = env->GetEquirectPath();

	std::unique_ptr<Image> equirect;

	// Try loading HDR file via stbi_loadf
	int w = 0, h = 0, c = 0;
	float* hdrData = stbi_loadf(hdrPath.c_str(), &w, &h, &c, 4);

	if (hdrData && w > 0 && h > 0)
	{
		NEURUS_LOG("[DeferredRenderer] Loaded HDR equirect: " << hdrPath
		           << " (" << w << "x" << h << ", " << c << " channels)");

		equirect = std::make_unique<Image>(
			m_device, m_physicalDevice,
			vk::Extent2D{static_cast<uint32_t>(w), static_cast<uint32_t>(h)},
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
			/*mipLevels=*/1,
			Image::ImageType::e2D,
			"IBL_Equirect_Event");

		const size_t dataSize = static_cast<size_t>(w)
		                      * static_cast<size_t>(h) * 4 * sizeof(float);
		equirect->UploadPixelData(m_device, m_physicalDevice,
		                          m_graphicsQueue, m_queueFamilyIndex,
		                          hdrData, dataSize);

		stbi_image_free(hdrData);
		NEURUS_LOG("[DeferredRenderer] HDR equirect uploaded to GPU");
	}
	else
	{
		NEURUS_LOG("[DeferredRenderer] HDR file not found: '"
		           << hdrPath << "', falling back to procedural gradient");
	}

	// Fallback: generate colourful procedural equirectangular gradient
	if (!equirect)
	{
		constexpr uint32_t eqWidth  = 1024;
		constexpr uint32_t eqHeight = 512;

		std::vector<float> pixels(eqWidth * eqHeight * 4, 0.0f);
		for (uint32_t y = 0; y < eqHeight; ++y)
		{
			for (uint32_t x = 0; x < eqWidth; ++x)
			{
				const float u = static_cast<float>(x) / static_cast<float>(eqWidth - 1);
				const float v = static_cast<float>(y) / static_cast<float>(eqHeight - 1);

				float r = 1.0f - u;
				float g = v;
				float b = u;

				const float equator = 1.0f - std::fabs(v - 0.5f) * 3.0f;
				if (equator > 0.0f)
				{
					r = std::max(r, equator);
					g = std::max(g, equator);
					b = std::max(b, equator);
				}

				const size_t idx = (y * eqWidth + x) * 4;
				pixels[idx + 0] = r;
				pixels[idx + 1] = g;
				pixels[idx + 2] = b;
				pixels[idx + 3] = 1.0f;
			}
		}

		equirect = std::make_unique<Image>(
			m_device, m_physicalDevice,
			vk::Extent2D{eqWidth, eqHeight},
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
			/*mipLevels=*/1,
			Image::ImageType::e2D,
			"IBL_Equirect_Fallback");

		const size_t dataSize = pixels.size() * sizeof(float);
		equirect->UploadPixelData(m_device, m_physicalDevice,
		                          m_graphicsQueue, m_queueFamilyIndex,
		                          pixels.data(), dataSize);

		NEURUS_LOG("[DeferredRenderer] Procedural gradient created ("
		           << eqWidth << "x" << eqHeight << ")");
	}

	// Generate IBL cubemaps from equirect
	if (m_iblPass && m_diffuseCubemap && m_specularCubemap)
	{
		m_iblPass->Generate(*equirect, *m_diffuseCubemap, *m_specularCubemap);
		EnableIBL();
		NEURUS_LOG("[DeferredRenderer] IBL cubemaps regenerated from environment event");
	}
	else
	{
		NEURUS_ERR("[DeferredRenderer] OnEnvironmentChanged: IBLPass or cubemaps not available");
	}

	// equirect Image destroyed here — GPU resources released
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
	vk::CommandBuffer cmdBuf = *m_commandBuffers[imageIndex];

	// No-args DrawFrame is deprecated and used only as camera-fallback.
	// Pass empty render items (no geometry drawn) - the fallback exists only
	// to prevent a crash when no camera is configured.
	recordFrame(cmdBuf, imageIndex, fallbackCam, {});

	auto& renderFinished = m_renderFinishedSemaphores[imageIndex];
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo(*imageAvailable, waitStage, cmdBuf, *renderFinished);
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
	vk::CommandBuffer cmdBuf = *m_commandBuffers[imageIndex];

	recordFrame(cmdBuf, imageIndex, *activeCam, renderItems);

	auto& renderFinished = m_renderFinishedSemaphores[imageIndex];
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo(*imageAvailable, waitStage, cmdBuf, *renderFinished);
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
	m_width = newExtent.width;
	m_height = newExtent.height;

	// Recreate attachments at new extent
	m_attachmentManager->Resize(newExtent);

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

void DeferredRenderer::recordFrame(vk::CommandBuffer cmdBuf, uint32_t imageIndex,
                                   const Camera& camera,
                                   const std::vector<GeometryRenderItem>& renderItems)
{
	const vk::Extent2D extent = m_swapchain->extent();

	// --- Begin command buffer ---
	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	cmdBuf.begin(beginInfo);

	// --- Transition G-Buffer images to attachment layouts for this frame ---
	//     Uses eUndefined as oldLayout to discard previous contents (beginRendering
	//     will clear them anyway). This explicit barrier ensures the validation
	//     layer's layout tracking is synchronised before dynamic rendering.
	{
		const std::array<AttachmentName, 4> gBufferColors = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,
			AttachmentName::MetallicRoughness,
		};

		std::array<vk::ImageMemoryBarrier2, 5> preBarriers;

		for (size_t i = 0; i < 4; ++i)
		{
			auto& att = m_attachmentManager->GetAttachment(gBufferColors[i]);
			preBarriers[i] = ImageBarrier(
				*att.ImageHandle(),
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::PipelineStageFlagBits2::eTopOfPipe,
				vk::AccessFlagBits2::eNone,
				vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				vk::AccessFlagBits2::eColorAttachmentWrite);
			att.SetCurrentLayout(vk::ImageLayout::eColorAttachmentOptimal);
		}

		const auto& depthAtt = m_attachmentManager->GetAttachment(AttachmentName::Depth);
		preBarriers[4] = ImageBarrier(
			*depthAtt.ImageHandle(),
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::PipelineStageFlagBits2::eTopOfPipe,
			vk::AccessFlagBits2::eNone,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::ImageAspectFlagBits::eDepth);

		const vk::DependencyInfo depInfo({}, {}, {}, preBarriers);
		cmdBuf.pipelineBarrier2(depInfo);
	}

	// --- Compute camera matrices for this frame ---
	const CameraUBOData cameraData = computeCameraData(extent, camera);
	const glm::mat4 viewMatrix = cameraData.view;
	const glm::vec3 cameraPos = camera.GetPosition();
	const glm::mat4 viewProj = cameraData.viewProj;

	// --- Phase 1: GeometryPass → G-Buffer MRT (using caller-provided render items) ---
	m_geometryPass->Record(cmdBuf, cameraData, renderItems, extent);

	// --- Phase 2: SSAO → compute ambient occlusion from G-Buffer ---
	if (m_ssaoPass)
	{
		m_ssaoPass->UpdateParams(viewProj, viewMatrix, cameraPos);
		m_ssaoPass->Record(cmdBuf, extent, m_currentFrame);
	}

	// --- Phase 3: LightingPass → compute PBR → HDRColor ---
	//     Light SSBO is owned and managed by LightingPass internally.
	//     UploadLights() should be called before the first DrawFrame()
	//     to populate the SSBO from the scene (handled by T9).
	// Compute inverse projection*view for skybox background ray in lighting pass
	const glm::mat4 invProjView = glm::inverse(cameraData.viewProj);
	m_lightingPass->Record(cmdBuf,
	                       cameraPos,
	                       viewMatrix,
	                       invProjView,
	                       extent,
	                       m_currentFrame);

	// --- Phase 4: Blit HDRColor → swapchain image ---
	// LightingPass leaves HDRColor in GENERAL layout.
	// Transition to TRANSFER_SRC_OPTIMAL for the blit.
	auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
	const vk::Image hdrImage = *hdrColor.ImageHandle();
	const vk::Image swapchainImage = m_swapchain->images()[imageIndex];

	// Barrier 1: HDRColor GENERAL → TRANSFER_SRC_OPTIMAL
	// Barrier 2: Swapchain image UNDEFINED → TRANSFER_DST_OPTIMAL
	{
		const auto hdrBarrier = ImageBarrier(
			hdrImage,
			vk::ImageLayout::eGeneral,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderWrite,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferRead);

		const auto scBarrier = ImageBarrier(
			swapchainImage,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits2::eTopOfPipe,
			vk::AccessFlagBits2::eNone,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite);

		const std::array<vk::ImageMemoryBarrier2, 2> barriers = { hdrBarrier, scBarrier };
		const vk::DependencyInfo depInfo({}, {}, {}, barriers);
		cmdBuf.pipelineBarrier2(depInfo);

		// Update CPU-side layout tracking
		hdrColor.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
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
		const auto barrier = ImageBarrier(
			swapchainImage,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite,
			vk::PipelineStageFlagBits2::eBottomOfPipe,
			vk::AccessFlagBits2::eNone);

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

	if (m_attachmentManager)
	{
		count = Screenshot::CaptureAllAttachments(m_device, m_physicalDevice,
		                                          m_graphicsQueue, m_queueFamilyIndex,
		                                          *m_attachmentManager,
		                                          "screenshots/gbuffer");
	}

	return count;
}

} // namespace neurus
