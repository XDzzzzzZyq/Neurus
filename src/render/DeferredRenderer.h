/**
 * @file DeferredRenderer.h
 * @brief Deferred rendering pipeline — GeometryPass → LightingPass → composite to swapchain.
 *
 * DeferredRenderer owns the full deferred pipeline and all scene GPU resources:
 *   - Swapchain management (reuses neurus::Swapchain)
 *   - AttachmentManager (G-Buffer, HDRColor)
 *   - GeometryPass (writes mesh geometry to G-Buffer MRT)
 *   - LightingPass (compute PBR lighting from G-Buffer into HDRColor)
 *   - Camera UBO data, mesh vertex/index buffers, light SSBO
 *   - Blit HDRColor to swapchain image for presentation
 *
 * Construction creates all Vulkan resources. DrawFrame() is the per-frame entry
 * point called from a QTimer in main.cpp.
 *
 * Non-copyable, non-movable (holds references to borrowed Vulkan objects).
 */

#pragma once

#include "GeometryPass.h"
#include "LightingPass.h"
#include "Swapchain.h"

#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace neurus {

// --- Forward declarations ---
class AttachmentManager;
class RenderPassManager;
class VertexBuffer;
class IndexBuffer;
class VulkanBuffer;

/**
 * @brief Deferred renderer orchestrating GeometryPass → LightingPass → composite.
 *
 * Usage:
 *   DeferredRenderer renderer(device, physDev, queue, qfi, surface,
 *                              w, h,
 *                              vertexData, vertexCount,
 *                              indexData, indexCount,
 *                              light,
 *                              gVertSpv, gVertSize,
 *                              gFragSpv, gFragSize,
 *                              lightCompSpv, lightCompSize);
 *   // Each frame:
 *   renderer.DrawFrame();
 */
class DeferredRenderer
{
public:
	/**
	 * @brief Creates the full deferred pipeline and uploads all scene data.
	 *
	 * Construction order: Swapchain → AttachmentManager → RenderPassManager →
	 * GeometryPass → LightingPass → scene GPU buffers → sync objects.
	 *
	 * @param device           Logical device (borrowed, must outlive this object).
	 * @param physicalDevice   Physical device (borrowed).
	 * @param graphicsQueue    Graphics queue for submits and staging uploads.
	 * @param queueFamilyIndex Queue family for command pool creation.
	 * @param surface          Presentation surface (borrowed, must outlive swapchain).
	 * @param width            Initial window width.
	 * @param height           Initial window height.
	 * @param vertexData       Interleaved vertex data (pos3+normal3+uv2 = 8 floats/vertex).
	 * @param vertexCount      Number of vertices.
	 * @param indexData        Index data (uint32_t array).
	 * @param indexCount       Number of indices.
	 * @param light            Single point light data for SSBO upload.
	 * @param gVertSpv         G-Buffer vertex shader SPIR-V data.
	 * @param gVertSize        G-Buffer vertex shader SPIR-V size.
	 * @param gFragSpv         G-Buffer fragment shader SPIR-V data.
	 * @param gFragSize        G-Buffer fragment shader SPIR-V size.
	 * @param lightCompSpv     PBR lighting compute shader SPIR-V data.
	 * @param lightCompSize    PBR lighting compute shader SPIR-V size.
	 */
	DeferredRenderer(const vk::raii::Device& device,
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
	                 size_t lightCompSize);

	~DeferredRenderer();

	// --- Non-copyable, non-movable ---
	DeferredRenderer(const DeferredRenderer&) = delete;
	DeferredRenderer& operator=(const DeferredRenderer&) = delete;
	DeferredRenderer(DeferredRenderer&&) = delete;
	DeferredRenderer& operator=(DeferredRenderer&&) = delete;

	/**
	 * @brief Draws a single frame: acquire → record → submit → present.
	 *
	 * Handles swapchain recreation on VK_ERROR_OUT_OF_DATE_KHR.
	 * Safe to call repeatedly (fence-guarded, max kMaxFramesInFlight in flight).
	 */
	void DrawFrame();

	/** @brief Blocks until all GPU work completes. */
	void WaitIdle();

	/** @brief Returns the current swapchain extent. */
	vk::Extent2D GetExtent() const { return m_swapchain ? m_swapchain->extent() : vk::Extent2D{800, 600}; }

	/**
	 * @brief Captures the current swapchain image to a timestamped PNG file.
	 *
	 * Uses the last acquired swapchain image index. Safe to call after a
	 * frame has been rendered. Blocks until GPU work completes.
	 *
	 * @return true if the screenshot was successfully written.
	 */
	bool TakeScreenshot();

	/**
	 * @brief Captures all G-Buffer attachments to timestamped PNG files.
	 *
	 * Dumps Position, Normal, Albedo, MetallicRoughness, and post-FX
	 * attachments to individual PNGs with timestamp in filename.
	 *
	 * @return Number of attachments successfully captured.
	 */
	int TakeScreenshotAllAttachments();

private:
	/**
	 * @brief Records the full deferred pipeline into a command buffer.
	 *
	 * Sequence:
	 *   1. GeometryPass::Record() → G-Buffer MRT
	 *   2. LightingPass::Record()  → compute PBR → HDRColor
	 *   3. Blit HDRColor → swapchain image
	 *   4. Transition swapchain image to present layout
	 */
	void recordFrame(vk::CommandBuffer cmdBuf, uint32_t imageIndex);

	/** @brief Destroys and re-creates sync objects after swapchain resize. */
	void recreateSwapchain();

	/**
	 * @brief Computes and returns camera UBO data for the current frame.
	 */
	CameraUBOData computeCameraData(vk::Extent2D extent) const;

	/**
	 * @brief Builds a single GeometryRenderItem for the sphere mesh.
	 */
	GeometryRenderItem buildRenderItem() const;

	/** @brief Creates the command pool (static helper for init-list use). */
	static vk::raii::CommandPool createCommandPool(const vk::raii::Device& device,
	                                               uint32_t queueFamilyIndex);

	// --- Borrowed objects ---
	const vk::raii::Device& m_device;
	const vk::raii::PhysicalDevice& m_physicalDevice;
	vk::Queue m_graphicsQueue;
	uint32_t m_queueFamilyIndex;
	const vk::raii::SurfaceKHR& m_surface;

	// --- Swapchain ---
	std::unique_ptr<Swapchain> m_swapchain;

	// --- Deferred pipeline ---
	std::unique_ptr<AttachmentManager> m_attachmentManager;
	std::unique_ptr<RenderPassManager> m_renderPassManager;
	std::unique_ptr<GeometryPass> m_geometryPass;
	std::unique_ptr<LightingPass> m_lightingPass;

	// --- Scene GPU resources ---
	std::unique_ptr<VertexBuffer> m_vertexBuffer;
	std::unique_ptr<IndexBuffer> m_indexBuffer;
	uint32_t m_indexCount = 0;
	std::unique_ptr<VulkanBuffer> m_lightSSBO;
	uint32_t m_lightCount = 1;

	// --- Camera state ---
	glm::vec3 m_cameraPos    = glm::vec3(0.0f, 2.0f, 5.0f);
	glm::vec3 m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
	float     m_cameraFov    = 60.0f;
	float     m_cameraNear   = 0.1f;
	float     m_cameraFar    = 100.0f;

	// --- Command pool ---
	vk::raii::CommandPool m_commandPool;

	// --- Synchronization ---
	static constexpr uint32_t kMaxFramesInFlight = 2;
	static constexpr uint64_t kFenceTimeoutNs = 100'000'000;

	std::vector<vk::raii::Fence> m_inFlightFences;
	std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
	std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
	uint32_t m_currentFrame = 0;
	uint32_t m_swapchainGeneration = 0;

	// --- Current swapchain extent ---
	uint32_t m_width = 800;
	uint32_t m_height = 600;

	// --- Last acquired swapchain image index (for screenshot) ---
	uint32_t m_lastImageIndex = 0;
};

} // namespace neurus
