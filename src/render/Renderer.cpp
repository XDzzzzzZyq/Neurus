#include "Renderer.h"
#include "Swapchain.h"
#include "shaders/ShaderProgram.h"

#include "Log.h"

#include <iostream>

namespace neurus {

Renderer::Renderer(const vk::raii::Device& device,
                   const vk::raii::PhysicalDevice& physicalDevice,
                   vk::Queue graphicsQueue,
                   uint32_t queueFamilyIndex,
                   const vk::raii::SurfaceKHR& surface,
                   uint32_t width, uint32_t height,
                   const uint32_t* vertSpv, size_t vertSize,
                   const uint32_t* fragSpv, size_t fragSize)
	: m_device(device)
	, m_graphicsQueue(graphicsQueue)
	, m_commandPool(createCommandPool(device, queueFamilyIndex))
	, m_width(width)
	, m_height(height)
{
	// --- Create swapchain ---
	m_swapchain = std::make_unique<Swapchain>(physicalDevice, device, surface, width, height);

	uint32_t imageCount = m_swapchain->imageCount();

	// --- Per-frame fences + acquire semaphores (CPU-GPU sync) ---
	for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
	{
		m_inFlightFences.emplace_back(device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
		m_imageAvailableSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
	}

	// --- Per-swapchain-image render-finished semaphores (must be indexed by acquire imageIndex) ---
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		m_renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
	}

	// --- Create shader program ---
	m_shaderProgram = std::make_unique<ShaderProgram>(device,
		vertSpv, vertSize, fragSpv, fragSize, m_swapchain->extent());

	// --- Create command buffers (one per swapchain image for simplicity) ---
	vk::CommandBufferAllocateInfo allocInfo(
		*m_commandPool,
		vk::CommandBufferLevel::ePrimary,
		m_swapchain->imageCount()
	);

	m_commandBuffers = vk::raii::CommandBuffers(device, allocInfo);

	// Record all command buffers initially
	for (uint32_t i = 0; i < m_swapchain->imageCount(); ++i)
	{
		recordCommandBuffer(*m_commandBuffers[i], i);
	}

	NEURUS_LOG("[Renderer] " << m_width << "x" << m_height
	          << " swapchainImages=" << imageCount
	          << " vertSpvSize=" << vertSize
	          << " fragSpvSize=" << fragSize);
}

Renderer::~Renderer()
{
	WaitIdle();
	// vk::raii handles cleanup automatically
}

void Renderer::DrawFrame()
{
	auto& fence = m_inFlightFences[m_currentFrame];
	auto& imageAvailable = m_imageAvailableSemaphores[m_currentFrame];

	// --- Wait for this frame slot's fence ---
	// Use a finite timeout so the main thread is never blocked indefinitely.
	// On timeout, skip this frame - the GPU hasn't finished the previous frame
	// for this slot, and Qt needs time to process window events.
	if (m_device.waitForFences(*fence, VK_TRUE, kFenceTimeoutNs) != vk::Result::eSuccess)
	{
		return;
	}

	// --- Acquire next swapchain image ---
	uint32_t imageIndex = 0;
	try
	{
		imageIndex = m_swapchain->AcquireNextImage(imageAvailable);
	}
		catch (const std::exception& e)
		{
			NEURUS_ERR("AcquireNextImage failed: " << e.what());
			// do NOT reset the fence so it remains signaled
			// for the next iteration. m_currentFrame is intentionally NOT advanced.
			return;
		}

	// Only reset the fence AFTER acquire succeeds.
	// If acquire failed above, the fence stays signaled to avoid a deadlock
	// where a reset-but-never-submitted fence blocks the main thread forever.
	m_device.resetFences(*fence);

	// --- Re-record command buffers if swapchain was recreated ---
	if (m_swapchain->generation() != m_swapchainGeneration)
	{
		// Resize render-finished semaphores to match new image count
		uint32_t newImageCount = m_swapchain->imageCount();
		m_renderFinishedSemaphores.clear();
		for (uint32_t i = 0; i < newImageCount; ++i)
		{
			m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
		}

		// Re-record ALL command buffers with the current swapchain image views
		// (must free and reallocate if image count changed)
		// WaitIdle ensures no command buffer is still in-flight on the GPU.
		WaitIdle();
		m_commandBuffers.clear();
		vk::CommandBufferAllocateInfo allocInfo(
			*m_commandPool,
			vk::CommandBufferLevel::ePrimary,
			newImageCount
		);
		m_commandBuffers = vk::raii::CommandBuffers(m_device, allocInfo);

		for (uint32_t i = 0; i < newImageCount; ++i)
		{
			recordCommandBuffer(*m_commandBuffers[i], i);
		}

		m_swapchainGeneration = m_swapchain->generation();
	}

	// --- Submit: render-finished semaphore indexed by swapchain image (required by spec) ---
	auto& renderFinished = m_renderFinishedSemaphores[imageIndex];
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	vk::SubmitInfo submitInfo(
		*imageAvailable,
		waitStage,
		*m_commandBuffers[imageIndex],
		*renderFinished
	);

	m_graphicsQueue.submit(submitInfo, *fence);

	// --- Present using same per-image render-finished semaphore ---
	try
	{
		m_swapchain->Present(renderFinished, imageIndex, m_graphicsQueue);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Present failed: " << e.what());
	}

	// --- Advance frame slot ---
	m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

void Renderer::WaitIdle()
{
	m_device.waitIdle();
}

void Renderer::recordCommandBuffer(vk::CommandBuffer cmdBuf, uint32_t imageIndex)
{
	// --- Begin command buffer ---
	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse);
	cmdBuf.begin(beginInfo);

	// --- Transition swapchain image to color attachment ---
	vk::ImageMemoryBarrier2 barrier(
		vk::PipelineStageFlagBits2::eTopOfPipe,
		vk::AccessFlagBits2::eNone,
		vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		vk::AccessFlagBits2::eColorAttachmentWrite,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eColorAttachmentOptimal,
		0, 0,  // queue family indices (no transfer)
		m_swapchain->images()[imageIndex],
		vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
	);

	vk::DependencyInfo barrierDependency({}, {}, {}, barrier);
	cmdBuf.pipelineBarrier2(barrierDependency);

	// --- Begin dynamic rendering ---
	vk::RenderingAttachmentInfo colorAttachment(
		*m_swapchain->imageViews()[imageIndex],
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ResolveModeFlagBits::eNone,
		nullptr,                                   // No resolve
		vk::ImageLayout::eUndefined,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eStore,
		vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.15f, 1.0f}))
	);

	vk::RenderingInfo renderingInfo(
		{},
		vk::Rect2D({0, 0}, m_swapchain->extent()),
		1,
		0,
		colorAttachment,
		nullptr,  // No depth attachment
		nullptr   // No stencil attachment
	);

	cmdBuf.beginRendering(renderingInfo);

	// --- Set dynamic viewport + scissor ---
	vk::Viewport viewport(0.0f, 0.0f,
	                      static_cast<float>(m_swapchain->extent().width),
	                      static_cast<float>(m_swapchain->extent().height),
	                      0.0f, 1.0f);
	cmdBuf.setViewport(0, viewport);

	vk::Rect2D scissor({0, 0}, m_swapchain->extent());
	cmdBuf.setScissor(0, scissor);

	// --- Bind pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_shaderProgram->pipeline());

	// --- Draw triangle (3 vertices, no vertex buffer) ---
	cmdBuf.draw(3, 1, 0, 0);

	// --- End dynamic rendering ---
	cmdBuf.endRendering();

	// --- Transition swapchain image to present layout ---
	vk::ImageMemoryBarrier2 presentBarrier(
		vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		vk::AccessFlagBits2::eColorAttachmentWrite,
		vk::PipelineStageFlagBits2::eBottomOfPipe,
		vk::AccessFlagBits2::eNone,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::ePresentSrcKHR,
		0, 0,
		m_swapchain->images()[imageIndex],
		vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
	);

	vk::DependencyInfo presentBarrierDependency({}, {}, {}, presentBarrier);
	cmdBuf.pipelineBarrier2(presentBarrierDependency);

	// --- End command buffer ---
	cmdBuf.end();
}

vk::raii::CommandPool Renderer::createCommandPool(const vk::raii::Device& device,
                                                    uint32_t queueFamilyIndex)
{
	vk::CommandPoolCreateInfo poolInfo(
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyIndex
	);

	return vk::raii::CommandPool(device, poolInfo);
}

} // namespace neurus
