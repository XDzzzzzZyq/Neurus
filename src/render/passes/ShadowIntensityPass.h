/**
 * @file ShadowIntensityPass.h
 * @brief Per-pixel point-light shadow intensity compute pass.
 *
 * Reads the G-Buffer world-space position and the point light's shadow
 * depth cubemap, computes per-pixel hard-shadow intensity (1.0 = fully
 * shadowed, 0.0 = fully lit), and writes the result to the ShadowIntensity
 * attachment (R8_UNORM) for consumption by the lighting pass.
 *
 * Architecture:
 * - Inherits from ComputePass for shared infrastructure (sampler, descriptor
 *   pool/sets, barrier transitions, dispatch logic).
 * - Owns the compute pipeline.
 * - Borrows RenderCache for G-Buffer Position, shadow cubemap, and shadow
 *   intensity output attachment.
 *
 * @note Hard shadow only (no PCF). Maps point light depth cubemap samples
 *       directly to binary shadow decisions via depth comparison.
 */

#pragma once

#include "passes/ComputePass.h"

#include <vulkan/vulkan_raii.hpp>

namespace neurus {

// --- Forward declarations ---
class RenderCache;

/**
 * @brief Point-light shadow intensity compute pass.
 *
 * Reads the G-Buffer Position as a combined image sampler and the point
 * light's shadow depth cubemap as a non-compare samplerCube.  Compares
 * the fragment-to-light distance against the stored cubemap depth and
 * writes a binary shadow result (R8_UNORM) to the ShadowIntensity
 * attachment.
 *
 * Inherits shared compute-pass infrastructure from ComputePass.
 */
class ShadowIntensityPass : public ComputePass
{
public:
	/**
	 * @brief Constructs the shadow intensity pass and creates all GPU resources.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for sampler creation).
	 * @param numSets           Number of descriptor sets (one per in-flight frame).
	 * @param graphicsQueue     Graphics queue (unused, kept for API symmetry).
	 * @param queueFamilyIndex  Queue family index (unused, kept for API symmetry).
	 * @param compSpv           Embedded compute shader SPIR-V data.
	 * @param compSize          Compute shader SPIR-V size in bytes.
	 *
	 * @throws std::runtime_error if shader or pipeline creation fails.
	 */
	ShadowIntensityPass(const vk::raii::Device& device,
	                    const vk::raii::PhysicalDevice& physicalDevice,
	                    uint32_t numSets,
	                    vk::Queue graphicsQueue,
	                    uint32_t queueFamilyIndex,
	                    const uint32_t* compSpv,
	                    size_t compSize);

	// -------------------------------------------------------------------
	// Recording
	// -------------------------------------------------------------------

	/**
	 * @brief Records the shadow intensity compute dispatch into a command buffer.
	 *
	 *   1. Stores the current light UID from ctx.
	 *   2. Writes descriptors for this frame slot.
	 *   3. Transitions G-Buffer Position to ColorShaderRead.
	 *   4. Transitions the shadow cubemap to DepthShaderRead.
	 *   5. Transitions ShadowIntensity to ShaderWrite.
	 *   6. Looks up the light world position from ctx.scene->light_list.
	 *   7. Binds pipeline, descriptor set, push constants.
	 *   8. Dispatches ceil(width/16) x ceil(height/16) x 1 thread groups.
	 *   9. Transitions ShadowIntensity to ColorShaderRead for lighting pass.
	 *
	 * Early-returns when ctx.scene is nullptr (no scene).
	 *
	 * @param cmdBuf  Command buffer in recording state.
	 * @param cache   Render cache for attachment/shadow map access.
	 * @param ctx     Per-frame context (render extent, frame index, scene).
	 */
	void Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx) override;

	/**
	 * @brief Writes all descriptors (image) into the specified set.
	 *
	 * Bindings:
	 *   0: G-Buffer Position (combined image sampler)
	 *   1: Shadow depth cubemap (combined image sampler, samplerCube)
	 *   2: Shadow intensity output (storage image, R8_UNORM)
	 *
	 * @param setIndex  Index into m_descriptorSets (0 … numSets-1).
	 */
	void WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache) override;

private:
	/**
	 * @brief Creates the descriptor set layout (3 bindings).
	 *
	 * Bindings:
	 *   0: gPosition       (combined image sampler)
	 *   1: u_ShadowCube    (combined image sampler)
	 *   2: outputShadow    (storage image, R8)
	 */
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);

	/**
	 * @brief Creates the compute pipeline via ComputePipelineBuilder.
	 */
	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device,
	                                  const uint32_t* compSpv,
	                                  size_t compSize);

	// --- Pipeline ---
	vk::raii::Pipeline m_pipeline;

	// --- Push constant values ---
	float m_bias = 0.0005f; ///< Depth bias for shadow acne prevention

	/// Two descriptor sets per in-flight frame slot so the per-light loop
	/// can alternate between them without updating a currently-bound set.
	static constexpr uint32_t kSetsPerFrameSlot = 2;

	// --- Current light UID (set before WriteDescriptors) ---
	int32_t m_currentLightUID = -1;
};

} // namespace neurus
