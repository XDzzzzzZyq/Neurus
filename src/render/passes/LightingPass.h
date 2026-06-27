/**
 * @file LightingPass.h
 * @brief PBR lighting pass - compute shader reading G-Buffer and evaluating
 *        Cook-Torrance GGX BRDF per point light with IBL support.
 *
 * LightingPass consumes the G-Buffer attachments written by GeometryPass and
 * evaluates direct PBR lighting + IBL ambient into a single HDR colour attachment.
 * Uses a compute shader dispatched at 16×16 thread groups.
 *
 * Architecture:
 * - Owns the compute pipeline, descriptor sets, sampler, descriptor pool,
 *   and light SSBO (GPUBuffer).
 * - Borrows RenderCache for G-Buffer and HDR colour image views.
 * - Reads IBL cubemap resources per-frame from the Scene's Environment list.
 * - Uses ComputePipelineBuilder for pipeline construction.
 *
 * @note Direct lighting + IBL (diffuse + specular).
 * @note Descriptor set layout: 10 bindings (5 sampled images, 1 storage image, 1 SSBO, 2 cube samplers, 1 shadow).
 */

#pragma once

#include "passes/ComputePass.h"
#include "../DescriptorManager.h"
#include "../buffers/GPUBuffer.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

// --- Forward declarations ---
class RenderCache;
class ComputePipelineBuilder;
class Image;
class Scene;

// ---------------------------------------------------------------------------
// GPU-side data structures (std140-compatible)
// ---------------------------------------------------------------------------

/**
 * @brief Point light data uploaded to the GPU SSBO.
 *
 * Layout matches the GLSL PointLight struct (std140, 48 bytes per element):
 *   vec3 color  (offset 0,  padded to 16)
 *   vec3 pos    (offset 16, padded to 16)
 *   float power (offset 32)
 *   float radius(offset 36)
 *   int32_t shadowMapIndex(offset 48)
 *   float _pad3(offset 52)
 *   Total: 64 bytes (aligned to 16, sizeof rounded up to 16-byte boundary).
 */
struct alignas(16) PointLightGpu
{
	float colorR, colorG, colorB;   ///< RGB colour (linear)
	float _pad0;                     ///< std140 padding after vec3
	float posX, posY, posZ;         ///< World-space position
	float _pad1;                     ///< std140 padding after vec3
	float power;                     ///< Luminous intensity
	float radius;                    ///< Physical radius
	float _pad2[2];                  ///< std140 padding
	int32_t shadowMapIndex = -1;     ///< Index into shadow maps array; -1 = no shadow
	float _pad3 = 0.0f;              ///< Padding to 64 bytes (16-byte aligned struct)
};
static_assert(sizeof(PointLightGpu) == 64, "PointLightGpu must be 64 bytes (std140)");

/**
 * @brief Push constants for the PBR lighting compute shader.
 *
 * Layout (matches GLSL push_constant block with std430 alignment):
 *   int  lightCount    offset 0   (4 bytes)
 *          padding     offset 4   (12 bytes)
 *   vec4 cameraPos     offset 16  (16 bytes)
 *   mat4 view          offset 32  (64 bytes)
 *   int  iblEnabled    offset 96  (4 bytes)
 *          padding     offset 100 (12 bytes, aligns mat4 to 16)
 *   mat4 invProjView   offset 112 (64 bytes - inverse(proj * view) for skybox ray)
 *   Total: 176 bytes. Must NOT use alignas.
 */
struct LightingPushConstants
{
	int32_t  lightCount;            ///< Number of active point lights in SSBO
	float    _pad0[3];              ///< Padding to align cameraPos at offset 16
	float    camX, camY, camZ;      ///< Camera world-space position
	float    _pad1;                 ///< Padding (vec4 → 16 bytes)
	float    view[16];              ///< View matrix (for normal transform VS→WS)
	int32_t  iblEnabled;            ///< IBL enabled flag (0 = disabled, 1 = enabled)
	float    _pad2[3];              ///< Padding to align invProjView at offset 112 (16-byte alignment)
	float    invProjView[16];       ///< Inverse of (projection * view) matrix for skybox ray
};
static_assert(sizeof(LightingPushConstants) == 176, "LightingPushConstants must be 176 bytes");

// ---------------------------------------------------------------------------
// LightingPass
// ---------------------------------------------------------------------------

/**
 * @brief PBR lighting compute pass.
 *
 * Reads the G-Buffer (Position, Normal, Albedo, MetallicRoughness) as
 * combined image samplers, iterates point lights from an own SSBO, evaluates
 * the Cook-Torrance GGX BRDF, and writes HDR colour to the output image.
 *
 * Non-copyable, movable.
 */
class LightingPass : public ComputePass
{
public:
	/**
	 * @brief Constructs the lighting pass and creates all GPU resources.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for sampler creation).
	 * @param numSets           Number of descriptor sets to allocate (one per
	 *                          in-flight frame). Must match kMaxFramesInFlight
	 *                          in the renderer.
	 * @param graphicsQueue     Graphics queue for light SSBO staging uploads.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 * @param compSpv           Embedded compute shader SPIR-V data.
	 * @param compSize          Compute shader SPIR-V size in bytes.
	 *
	 * @throws std::runtime_error if shader or pipeline creation fails.
	 */
	LightingPass(const vk::raii::Device& device,
	             const vk::raii::PhysicalDevice& physicalDevice,
	             uint32_t numSets,
	             vk::Queue graphicsQueue,
	             uint32_t queueFamilyIndex,
	             const uint32_t* compSpv,
	             size_t compSize);

	~LightingPass() override;

	LightingPass(LightingPass&&) noexcept = default;
	LightingPass& operator=(LightingPass&&) noexcept = default;

	// -------------------------------------------------------------------
	// Light SSBO management
	// -------------------------------------------------------------------

	/**
	 * @brief Converts scene point lights to PointLightGpu and uploads as SSBO.
	 *
	 * Iterates scene.light_list, filters to POINTLIGHT type, converts
	 * each Light to a PointLightGpu struct (std140, 48 bytes), and
	 * uploads the array as a device-local storage buffer.
	 *
	 * If the scene has no point lights, the light SSBO is released,
	 * the descriptor binding uses PARTIALLY_BOUND (no update when null),
	 * and GetLightCount() returns 0.
	 *
	 * @param scene Scene containing the light list.
	 */
	void UploadLights(const Scene& scene);

	/**
	 * @brief Returns the light SSBO or nullptr when no lights are present.
	 *
	 * When nullptr, descriptor binding 5 uses PARTIALLY_BOUND and is
	 * not updated — the shader never reads it because lightCount=0.
	 *
	 * @return Non-owning pointer to GPUBuffer, or nullptr.
	 */
	const GPUBuffer* GetLightSSBO() const;

	/**
	 * @brief Returns the number of point lights in the SSBO.
	 * @return Light count (0 if no lights uploaded).
	 */
	uint32_t GetLightCount() const;

	// -------------------------------------------------------------------
	// Recording
	// -------------------------------------------------------------------

	/**
	 * @brief Records the PBR lighting compute dispatch into a command buffer.
	 *
	 *   1. Transitions G-Buffer images to SHADER_READ_ONLY_OPTIMAL.
	 *   2. Transitions HDRColor output to GENERAL.
	 *   3. Writes descriptors into the descriptor set for this frame slot.
	 *   4. Binds the compute pipeline, descriptor set, and push constants.
	 *   5. Dispatches ceil(width/16) × ceil(height/16) × 1 thread groups.
	 *   6. Inserts a memory barrier to make the output visible.
	 *
	 * @param cmdBuf          Command buffer in recording state.
	 * @param ctx             Per-frame context (camera position, view matrix,
	 *                        invProjView, render extent, frame index).
	 */
	void Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx) override;

private:
	/**
	 * @brief Creates the descriptor set layout (9 bindings).
	 *
	 * Bindings:
	 *   0: gPosition           (combined image sampler)
	 *   1: gNormal              (combined image sampler)
	 *   2: gAlbedo              (combined image sampler)
	 *   3: gMetallicRoughness   (combined image sampler)
	 *   4: outputImage          (storage image)
	 *   5: LightBuffer          (storage buffer / SSBO, PARTIALLY_BOUND)
	 *   6: U_AO                 (combined image sampler, SSAO occlusion)
	 *   7: U_Irradiance         (combined image sampler, diffuse IBL cubemap)
	 *   8: U_Prefiltered        (combined image sampler, specular IBL cubemap)
	 */
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);

	/**
	 * @brief Creates the compute pipeline via ComputePipelineBuilder.
	 */
	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device,
	                                  const uint32_t* compSpv,
	                                  size_t compSize);

	/**
	 * @brief Writes all descriptors (image + buffer) into the specified set.
	 *
	 * Called every frame during Record(). One set per in-flight frame
	 * prevents updating a set while the GPU is still reading it.
	 *
	 * @param setIndex  Index into m_descriptorSets (0 … numSets-1).
	 */
	void WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache) override;

	// --- Queue handles for SSBO creation ---
	vk::Queue m_graphicsQueue;
	uint32_t m_queueFamilyIndex;

	// --- Pipeline ---
	vk::raii::Pipeline m_pipeline;

	// --- Owned light SSBO ---
	std::unique_ptr<GPUBuffer> m_lightSSBO;
	uint32_t m_lightCount = 0;

	// --- IBL cubemap fallback (4×4 black cubemap, valid when no IBL set) ---
	std::unique_ptr<Image> m_fallbackIrradianceCube;
	std::unique_ptr<Image> m_fallbackPrefilteredCube;
	vk::raii::Sampler m_fallbackCubeSampler = nullptr;

	// --- Current light UID (set before WriteDescriptors) ---
	int32_t m_currentLightUID = -1;
};
} // namespace neurus
