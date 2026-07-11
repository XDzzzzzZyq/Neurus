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
#include "../shaders/ShaderLibrary.h"
#include "../shaders/ComputeShader.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <unordered_map>

namespace neurus {

// --- Forward declarations ---
class RenderCache;
class ComputePipelineBuilder;
class Texture;

// ---------------------------------------------------------------------------
// LightingPass
// ---------------------------------------------------------------------------

/**
 * @brief PBR lighting compute pass.
 *
 * Reads the G-Buffer (Position, Normal, Albedo, MetallicRoughness) as
 * combined image samplers, dispatches the PBR lighting compute shader,
 * and writes HDR colour to the output image.  Light SSBOs are now owned
 * by LightingGPU (stored in RenderCache) rather than LightingPass itself.
 *
 * Non-copyable, movable.
 */
class LightingPass : public ComputePass
{
public:
	/**
	 * @brief Constructs the lighting pass and creates all GPU resources.
	 *
	 * Shaders are self-loaded via ShaderLibrary from GLSL source files.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for sampler creation).
	 * @param numSets           Number of descriptor sets to allocate (one per
	 *                          in-flight frame). Must match kMaxFramesInFlight
	 *                          in the renderer.
	 *
	 * @throws std::runtime_error if shader or pipeline creation fails.
	 */
	LightingPass(const vk::raii::Device& device,
	             const vk::raii::PhysicalDevice& physicalDevice,
	             uint32_t numSets);

	~LightingPass() override;

	LightingPass(LightingPass&&) noexcept = default;
	LightingPass& operator=(LightingPass&&) noexcept = default;

	/// @brief Maximum number of shadow-casting lights (sampler2DArray layers).
	static constexpr uint32_t MAX_SHADOW_LIGHTS = 4;

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

	/**
	 * @brief Creates the descriptor set layout (10 bindings).
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
	 *   9: U_ShadowArray        (combined image sampler, sampler2DArray)
	 */
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);

private:

	/**
	 * @brief Creates the compute pipeline via ComputePipelineBuilder.
	 * ShaderModule from self-loaded ComputeShader.
	 */
	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device);

	/**
	 * @brief Writes all descriptors (image + buffer) into the specified set.
	 *
	 * Light SSBOs are read from RenderCache::GetLightingGPU() at binding
	 * time, not stored locally.
	 *
	 * @param setIndex  Index into p_descriptorSets (0 … numSets-1).
	 */
	void WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache) override;

	// --- Pipeline ---
	vk::raii::Pipeline p_pipeline;

	// --- Self-loaded compute shader (via ShaderLibrary) ---
	std::shared_ptr<ComputeShader> p_computeShader;

	// --- Empty cubemap placeholder for IBL bindings when no env exists (1x1x6, black) ---
	std::unique_ptr<Texture> p_emptyCube;
	bool p_emptyInitialized = false;
};
} // namespace neurus
