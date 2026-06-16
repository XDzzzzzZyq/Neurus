/**
 * @file DeferredRenderer.cpp
 * @brief Deferred rendering pipeline implementation.
 */

#include "DeferredRenderer.h"

#include "AttachmentManager.h"
#include "GeometryPass.h"
#include "LightingPass.h"
#include "RenderPassManager.h"
#include "Screenshot.h"
#include "Swapchain.h"
#include "SyncObjects.h"
#include "VulkanBuffer.h"
#include "buffers/VertexBuffer.h"
#include "buffers/IndexBuffer.h"

#include <glm/gtc/matrix_transform.hpp>

#include "Log.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

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
                                   uint32_t height,
                                   const float* vertexData,
                                   uint32_t vertexCount,
                                   const uint32_t* indexData,
                                   uint32_t indexCount,
                                   const PointLightGpu& light,
                                   const uint32_t* gVertSpv,
                                   size_t gVertSize,
                                   const uint32_t* gFragSpv,
                                   size_t gFragSize,
                                   const uint32_t* lightCompSpv,
                                   size_t lightCompSize)
	: m_device(device)
	, m_physicalDevice(physicalDevice)
	, m_graphicsQueue(graphicsQueue)
	, m_queueFamilyIndex(queueFamilyIndex)
	, m_surface(surface)
	, m_width(width)
	, m_height(height)
	, m_indexCount(indexCount)
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

	// --- 4. Upload mesh vertex / index data ---
	const vk::DeviceSize vertexDataSize = static_cast<vk::DeviceSize>(vertexCount) * 8 * sizeof(float);
	constexpr uint32_t kVertexStride = 8 * sizeof(float);  // pos(3) + normal(3) + uv(2)

	m_vertexBuffer = std::make_unique<VertexBuffer>(
		device, physicalDevice, graphicsQueue, queueFamilyIndex,
		vertexData, vertexDataSize, kVertexStride, vertexCount);

	const vk::DeviceSize indexDataSize = static_cast<vk::DeviceSize>(indexCount) * sizeof(uint32_t);
	m_indexBuffer = std::make_unique<IndexBuffer>(
		device, physicalDevice, graphicsQueue, queueFamilyIndex,
		indexData, indexDataSize, indexCount);

	// --- 5. Upload light SSBO ---
	m_lightSSBO = std::make_unique<VulkanBuffer>(
		device, physicalDevice, graphicsQueue, queueFamilyIndex,
		sizeof(PointLightGpu),
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal);
	m_lightSSBO->Upload(&light, sizeof(PointLightGpu));

	// --- 6. Create geometry pass ---
	m_geometryPass = std::make_unique<GeometryPass>(
		device, physicalDevice, graphicsQueue, queueFamilyIndex,
		*m_attachmentManager, *m_renderPassManager,
		gVertSpv, gVertSize,
		gFragSpv, gFragSize);

	// --- 7. Create lighting pass ---
	m_lightingPass = std::make_unique<LightingPass>(
		device, physicalDevice,
		*m_attachmentManager,
		kMaxFramesInFlight,
		lightCompSpv, lightCompSize);

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
	          << " vertices=" << vertexCount
	          << " indices=" << indexCount
	          << " lightPower=" << light.power
	          << " lightRadius=" << light.radius
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

CameraUBOData DeferredRenderer::computeCameraData(vk::Extent2D extent) const
{
	const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

	const glm::mat4 view = glm::lookAt(m_cameraPos, m_cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 proj = glm::perspective(glm::radians(m_cameraFov), aspect, m_cameraNear, m_cameraFar);

	CameraUBOData data;
	data.viewProj = proj * view;
	data.view = view;
	return data;
}

// ---------------------------------------------------------------------------
// Render item construction
// ---------------------------------------------------------------------------

GeometryRenderItem DeferredRenderer::buildRenderItem() const
{
	GeometryRenderItem item;
	item.vertexBuffer = m_vertexBuffer->buffer();
	item.indexBuffer = m_indexBuffer->buffer();
	item.indexCount = m_indexCount;
	item.indexType = vk::IndexType::eUint32;

	// Identity model matrix (sphere at origin).
	// The icosphere OBJ has radius ~7.44 — scale down to fit the view
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

	recordFrame(cmdBuf, imageIndex);

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

void DeferredRenderer::WaitIdle()
{
	m_device.waitIdle();
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
	}

	m_swapchainGeneration = m_swapchain->generation();
}

// ---------------------------------------------------------------------------
// Frame recording
// ---------------------------------------------------------------------------

void DeferredRenderer::recordFrame(vk::CommandBuffer cmdBuf, uint32_t imageIndex)
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
			const auto& att = m_attachmentManager->GetAttachment(gBufferColors[i]);
			preBarriers[i] = ImageBarrier(
				*att.ImageHandle(),
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::PipelineStageFlagBits2::eTopOfPipe,
				vk::AccessFlagBits2::eNone,
				vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				vk::AccessFlagBits2::eColorAttachmentWrite);
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
	const CameraUBOData cameraData = computeCameraData(extent);
	const glm::mat4 viewMatrix = cameraData.view;

	// --- Build render item (single sphere mesh) ---
	const GeometryRenderItem renderItem = buildRenderItem();
	const std::vector<GeometryRenderItem> renderItems = { renderItem };

	// --- Phase 1: GeometryPass → G-Buffer MRT ---
	m_geometryPass->Record(cmdBuf, cameraData, renderItems, extent);

	// --- Phase 2: LightingPass → compute PBR → HDRColor ---
	m_lightingPass->Record(cmdBuf,
	                       *m_lightSSBO,
	                       m_lightCount,
	                       m_cameraPos,
	                       viewMatrix,
	                       extent,
	                       m_currentFrame);

	// --- Phase 3: Blit HDRColor → swapchain image ---
	// LightingPass leaves HDRColor in GENERAL layout.
	// Transition to TRANSFER_SRC_OPTIMAL for the blit.
	const auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
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
	if (!m_attachmentManager)
	{
		return 0;
	}

	return Screenshot::CaptureAllAttachments(m_device, m_physicalDevice,
	                                          m_graphicsQueue, m_queueFamilyIndex,
	                                          *m_attachmentManager,
	                                          "screenshots/gbuffer");
}

} // namespace neurus
