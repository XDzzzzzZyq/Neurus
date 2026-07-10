/**
 * @file DeferredRenderer.h
 * @brief Deferred rendering pipeline - GeometryPass → LightingPass → composite to swapchain.
 *
 * DeferredRenderer owns the full deferred pipeline and all scene GPU resources:
 *   - Swapchain management (reuses neurus::Swapchain)
 *   - RenderCache (G-Buffer, HDRColor)
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

#include "passes/Pass.h"
#include "RenderConfig.h"
#include "Swapchain.h"

#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace neurus {

// --- Forward declarations ---
class RenderCache;
class VertexBuffer;
class IndexBuffer;
class GPUBuffer;
class Camera;
class Scene;
class Mesh;
class Image;
class Environment;
class GeometryPass;
class LightingPass;
class SSAOPass;
class IBLPass;
class ShadowDepthPass;
class ShadowIntensityPass;
struct CameraUBOData;

/**
 * @brief Deferred renderer orchestrating GeometryPass → LightingPass → composite.
 *
 * Usage:
 *   DeferredRenderer renderer(device, physDev, queue, qfi, surface,
 *                              w, h);
 *   // SPIR-V shaders are embedded in DeferredRenderer.cpp - no params needed.
 *   // Each frame:
 *   renderer.DrawFrame();
 */
class DeferredRenderer
{
public:
	/**
	 * @brief Creates the full deferred pipeline.
	 *
	 * Construction order: Swapchain → RenderCache →
	 * GeometryPass → LightingPass → sync objects. Mesh buffers are uploaded
	 * directly to GPU by Mesh objects. LightingPass owns its own light SSBO.
	 * SPIR-V shaders are embedded at compile time (no constructor parameters needed).
	 *
	 * @param device           Logical device (borrowed, must outlive this object).
	 * @param physicalDevice   Physical device (borrowed).
	 * @param graphicsQueue    Graphics queue for submits and staging uploads.
	 * @param queueFamilyIndex Queue family for command pool creation.
	 * @param surface          Presentation surface (borrowed, must outlive swapchain).
	 * @param width            Initial window width.
	 * @param height           Initial window height.
	 */
	DeferredRenderer(const vk::raii::Device& device,
	                 const vk::raii::PhysicalDevice& physicalDevice,
	                 vk::Queue graphicsQueue,
	                 uint32_t queueFamilyIndex,
	                 const vk::raii::SurfaceKHR& surface,
	                 uint32_t width,
	                 uint32_t height);

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
	 * @brief Uploads scene point lights to the LightingPass SSBO and configures
	 *        the shadow passes for the first point light found.
	 *
	 * Converts scene.light_list to GPU-compatible PointLightGpu structs
	 * and uploads them as a storage buffer. Must be called before the
	 * first DrawFrame() and after any scene light changes.
	 *
	 * If a point light with use_shadow enabled is found, configures
	 * ShadowDepthPass accordingly.
	 *
	 * @param scene Scene containing the light list.
	 */
	void UploadLights(const Scene& scene);

	/** @brief Blocks until all GPU work completes. */
	void WaitIdle();

	/** @brief Returns the current swapchain extent. */
	vk::Extent2D GetExtent() const { return r_swapchain ? r_swapchain->extent() : vk::Extent2D{800, 600}; }

	/**
	 * @brief Returns the last acquired swapchain image for screenshot capture.
	 *
	 * The returned image is in PRESENT_SRC layout from the most recent frame.
	 * Caller must wait for GPU idle before using the image for readback.
	 *
	 * @return vk::Image handle (VK_NULL_HANDLE if swapchain not available or
	 *         image index is invalid).
	 */
	vk::Image GetLastSwapchainImage() const;

	/** @brief Returns the current swapchain image format. */
	vk::Format GetSwapchainFormat() const;

	/**
	 * @brief Returns a reference to the RenderCache for external access.
	 *
	 * Used by the Screenshot class to enumerate and capture attachments,
	 * shadow maps, and shadow intensity arrays.
	 */
	RenderCache& GetRenderCache() { return *r_renderCache; }

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

	/**
	 * @brief Generates IBL cubemaps for an environment and caches them in RenderCache.
	 *
	 * Delegates to RenderCache::CreateEnvironmentGPU() which loads the equirect
	 * ImageData, creates cubemap Images + samplers, and runs IBLPass convolution.
	 * The resulting EnvironmentGPU is stored in RenderCache for per-frame use
	 * by LightingPass.
	 *
	 * @param env Shared pointer to the CPU-side Environment (provides equirect data).
	 */
	void GenerateIBL(const std::shared_ptr<Environment>& env);

private:
	/**
	 * @brief Records the full deferred pipeline into a command buffer.
	 *
	 * Sequence:
	 *   1. GeometryPass::Record() → G-Buffer MRT (reads scene.mesh_list via MeshGPU)
	 *   2. ShadowDepthPass::Record() → cubemap depth (reads scene.mesh_list via MeshGPU)
	 *   3. LightingPass::Record() → compute PBR → HDRColor (uses own light SSBO)
	 *   4. Blit HDRColor → swapchain image
	 *   5. Transition swapchain image to present layout
	 *
	 * @param scene Scene providing active camera and mesh/light lists for iteration.
	 */
	void recordFrame(const vk::raii::CommandBuffer& cmdBuf, uint32_t imageIndex,
	                 const Scene& scene);

	/** @brief Destroys and re-creates sync objects after swapchain resize. */
	void recreateSwapchain();

	/** @brief Creates the command pool (static helper for init-list use). */
	static vk::raii::CommandPool createCommandPool(const vk::raii::Device& device,
	                                               uint32_t queueFamilyIndex);

	// --- Borrowed objects ---
	const vk::raii::Device& r_device;
	const vk::raii::PhysicalDevice& r_physicalDevice;
	vk::Queue r_graphicsQueue;
	uint32_t r_queueFamilyIndex;

	// --- Render config (user-settable pipeline options) ---
	RenderConfig r_config;

	// --- Swapchain ---
	std::unique_ptr<Swapchain> r_swapchain;

	// --- Deferred pipeline ---
	std::unique_ptr<RenderCache> r_renderCache;
	// --- Polymorphic pass container (owning) ---
	std::vector<std::unique_ptr<Pass>> r_passes;

	// --- Cached raw pointers for zero-cost access (non-owning) ---
	GeometryPass* r_geometryPass = nullptr;
	LightingPass* r_lightingPass = nullptr;
	SSAOPass*   r_ssaoPass     = nullptr;
	IBLPass*    r_iblPass      = nullptr;
	ShadowDepthPass* r_shadowDepthPass = nullptr;
	ShadowIntensityPass* r_shadowIntensityPass = nullptr;

	// --- Command pool ---
	vk::raii::CommandPool r_commandPool;

	// --- Command buffers (one per swapchain image, reused each frame) ---
	std::vector<vk::raii::CommandBuffer> r_commandBuffers;

	// --- Synchronization ---
	static constexpr uint32_t kMaxFramesInFlight = 2;
	static constexpr uint64_t kFenceTimeoutNs = 100'000'000;

	std::vector<vk::raii::Fence> r_inFlightFences;
	std::vector<vk::raii::Semaphore> r_imageAvailableSemaphores;
	std::vector<vk::raii::Semaphore> r_renderFinishedSemaphores;
	uint32_t r_currentFrame = 0;
	uint32_t r_swapchainGeneration = 0;

	// --- Last acquired swapchain image index ---
	uint32_t r_lastImageIndex = 0;

};

} // namespace neurus
