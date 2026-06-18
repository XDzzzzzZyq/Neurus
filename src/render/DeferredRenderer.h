/**
 * @file DeferredRenderer.h
 * @brief Deferred rendering pipeline - GeometryPass → LightingPass → composite to swapchain.
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
class Camera;
class Scene;
class Mesh;

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
	 * @brief Creates the full deferred pipeline.
	 *
	 * Construction order: Swapchain → AttachmentManager → RenderPassManager →
	 * GeometryPass → LightingPass → sync objects. Mesh buffers are uploaded
	 * directly to GPU by Mesh objects. LightingPass owns its own light SSBO.
	 *
	 * @param device           Logical device (borrowed, must outlive this object).
	 * @param physicalDevice   Physical device (borrowed).
	 * @param graphicsQueue    Graphics queue for submits and staging uploads.
	 * @param queueFamilyIndex Queue family for command pool creation.
	 * @param surface          Presentation surface (borrowed, must outlive swapchain).
	 * @param width            Initial window width.
	 * @param height           Initial window height.
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
	 * Uses a fallback default camera (positioned at (0,2,5), looking at origin).
	 * Handles swapchain recreation on VK_ERROR_OUT_OF_DATE_KHR.
	 * Safe to call repeatedly (fence-guarded, max kMaxFramesInFlight in flight).
	 *
	 * @deprecated Use DrawFrame(const Scene&) to provide scene-defined camera.
	 */
	void DrawFrame();

	/**
	 * @brief Draws a single frame using the scene's active camera.
	 *
	 * Reads camera transform and projection from scene.GetActiveCamera().
	 * Falls back to default camera if no active camera is set.
	 *
	 * @param scene Scene providing the active camera for this frame.
	 */
	void DrawFrame(const Scene& scene);

	/**
	 * @brief Uploads scene point lights to the LightingPass SSBO.
	 *
	 * Converts scene.light_list to GPU-compatible PointLightGpu structs
	 * and uploads them as a storage buffer. Must be called before the
	 * first DrawFrame() and after any scene light changes.
	 *
	 * @param scene Scene containing the light list.
	 */
	void UploadLights(const Scene& scene);

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

	/**
	 * @brief Handles window resize by proactively recreating swapchain and
	 *        dependent resources.
	 *
	 * Calls Swapchain::Recreate() with the new dimensions, then rebuilds
	 * attachments, semaphores, and command buffers. This avoids waiting for
	 * VK_ERROR_OUT_OF_DATE_KHR on the next AcquireNextImage, preventing a
	 * one-frame rendering glitch at the wrong size.
	 *
	 * Safe to call multiple times (idempotent for same dimensions).
	 * No-op if the window is minimized (zero-area surface).
	 *
	 * @param width  New window width in pixels.
	 * @param height New window height in pixels.
	 */
	void HandleResize(uint32_t width, uint32_t height);

private:
	/**
	 * @brief Records the full deferred pipeline into a command buffer.
	 *
	 * Sequence:
	 *   1. GeometryPass::Record(renderItems) → G-Buffer MRT
	 *   2. LightingPass::Record() → compute PBR → HDRColor (uses own light SSBO)
	 *   3. Blit HDRColor → swapchain image
	 *   4. Transition swapchain image to present layout
	 *
	 * @param renderItems Pre-built render items from scene meshes (may be empty).
	 */
	void recordFrame(vk::CommandBuffer cmdBuf, uint32_t imageIndex,
	                 const Camera& camera,
	                 const std::vector<GeometryRenderItem>& renderItems);

	/** @brief Destroys and re-creates sync objects after swapchain resize. */
	void recreateSwapchain();

	/**
	 * @brief Computes and returns camera UBO data for the current frame.
	 */
	CameraUBOData computeCameraData(vk::Extent2D extent, const Camera& camera) const;

	/**
	 * @brief Builds a GeometryRenderItem for the specified mesh from its GPU buffers.
	 * @param mesh Mesh providing GPU vertex/index buffers via GetVertexBuffer/GetIndexBuffer.
	 * @return GeometryRenderItem with buffers from mesh, or default item if mesh GPU buffers unavailable.
	 */
	GeometryRenderItem buildRenderItem(const Mesh& mesh) const;

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

	// --- Fallback SSBO for zero-light scenes (LightingPass needs a valid ref) ---
	std::unique_ptr<VulkanBuffer> m_fallbackSSBO;

	// --- Command pool ---
	vk::raii::CommandPool m_commandPool;

	// --- Command buffers (one per swapchain image, reused each frame) ---
	std::vector<vk::raii::CommandBuffer> m_commandBuffers;

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
