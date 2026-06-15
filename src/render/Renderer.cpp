#include "Renderer.h"
#include "Swapchain.h"
#include "ShaderProgram.h"

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
	, m_imageAvailable(device, vk::SemaphoreCreateInfo())
	, m_renderFinished(device, vk::SemaphoreCreateInfo())
	, m_inFlightFence(device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled))
	, m_width(width)
	, m_height(height)
{
	// --- Create swapchain ---
	m_swapchain = std::make_unique<Swapchain>(physicalDevice, device, surface, width, height);

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
}

Renderer::~Renderer()
{
	WaitIdle();
	// vk::raii handles cleanup automatically
}

void Renderer::DrawFrame()
{
	// --- Wait for previous frame to complete ---
	if (m_device.waitForFences(*m_inFlightFence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
	{
		return;  // Device lost or error — application should handle this
	}
	m_device.resetFences(*m_inFlightFence);

	// --- Acquire next swapchain image ---
	uint32_t imageIndex = 0;
	try
	{
		imageIndex = m_swapchain->AcquireNextImage(m_imageAvailable);
	}
	catch (const std::runtime_error&)
	{
		// Swapchain recreation failed or device lost
		return;
	}

	// --- Submit command buffer ---
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	vk::SubmitInfo submitInfo(
		*m_imageAvailable,
		waitStage,
		*m_commandBuffers[imageIndex],
		*m_renderFinished
	);

	m_graphicsQueue.submit(submitInfo, *m_inFlightFence);

	// --- Present ---
	try
	{
		m_swapchain->Present(m_renderFinished, imageIndex);
	}
	catch (...)
	{
		// Presentation failed — skip this frame
	}
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
		m_swapchain->imageViews()[imageIndex].getImage(),
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
		m_swapchain->imageViews()[imageIndex].getImage(),
		vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
	);

	cmdBuf.pipelineBarrier2(presentBarrier);

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
