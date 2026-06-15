#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

class Swapchain;
class ShaderProgram;

/**
 * @brief Public rendering API — owns the render loop and GPU resources.
 *
 * Created with a shared VulkanContext (device/queue) and a borrowed surface.
 * Constructs swapchain, pipeline, command pool, and synchronization primitives.
 *
 * Usage:
 *   Renderer renderer(vkContext, surface, width, height, vertSpv, vertSize, fragSpv, fragSize);
 *   // Each frame:
 *   renderer.DrawFrame();  // May be called from EventBus signal
 *   // On shutdown:
 *   renderer.WaitIdle();
 */
class Renderer
{
public:
	/**
	 * @brief Creates all rendering resources and prepares the pipeline.
	 *
	 * @param device Borrowed logical device (outlives Renderer).
	 * @param physicalDevice Borrowed physical device (for swapchain queries).
	 * @param graphicsQueue Borrowed queue handle (outlives Renderer).
	 * @param queueFamilyIndex Queue family for command pool creation.
	 * @param surface Borrowed surface for swapchain (outlives Renderer).
	 * @param width Initial window width.
	 * @param height Initial window height.
	 * @param vertSpv Vertex shader SPIR-V bytecode.
	 * @param vertSize Size of vertex shader SPIR-V in bytes.
	 * @param fragSpv Fragment shader SPIR-V bytecode.
	 * @param fragSize Size of fragment shader SPIR-V in bytes.
	 */
	Renderer(const vk::raii::Device& device,
	         const vk::raii::PhysicalDevice& physicalDevice,
	         vk::Queue graphicsQueue,
	         uint32_t queueFamilyIndex,
	         const vk::raii::SurfaceKHR& surface,
	         uint32_t width, uint32_t height,
	         const uint32_t* vertSpv, size_t vertSize,
	         const uint32_t* fragSpv, size_t fragSize);
	~Renderer();

	// Non-copyable — owns GPU resources
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// Non-movable (owned members with references to borrowed objects)
	Renderer(Renderer&&) = delete;
	Renderer& operator=(Renderer&&) = delete;

	/**
	 * @brief Draws a single frame.
	 *
	 * Acquires a swapchain image, records and submits command buffer,
	 * and presents the result. Handles swapchain recreation on resize.
	 *
	 * @note Called each frame from EventBus::renderRequested signal.
	 * @note May throw if the swapchain needs recreation — caller should retry.
	 */
	void DrawFrame();

	/**
	 * @brief Waits for all GPU operations to complete.
	 * Call before destroying the renderer or application shutdown.
	 */
	void WaitIdle();

private:
	/**
	 * @brief Records rendering commands into the command buffer.
	 *
	 * Uses vkCmdBeginRendering (dynamic rendering) to set up the render pass,
	 * binds the graphics pipeline, and draws 3 vertices.
	 */
	void recordCommandBuffer(vk::CommandBuffer cmdBuf, uint32_t imageIndex);

	/** @brief Creates the command pool for the given queue family. */
	static vk::raii::CommandPool createCommandPool(const vk::raii::Device& device,
	                                                uint32_t queueFamilyIndex);

	const vk::raii::Device& m_device;
	vk::Queue m_graphicsQueue;

	// Owned resources
	std::unique_ptr<Swapchain> m_swapchain;
	std::unique_ptr<ShaderProgram> m_shaderProgram;
	vk::raii::CommandPool m_commandPool;
	std::vector<vk::raii::CommandBuffer> m_commandBuffers;

	// Synchronization — one set per in-flight frame (double buffering)
	static constexpr uint32_t kMaxFramesInFlight = 2;
	std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
	std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
	std::vector<vk::raii::Fence> m_inFlightFences;
	uint32_t m_currentFrame = 0;

	// Current state
	uint32_t m_width = 800;
	uint32_t m_height = 600;
};

} // namespace neurus
