/**
 * @file SSAOPass.h
 * @brief Screen-Space Ambient Occlusion compute pass.
 *
 * SSAOPass reads the G-Buffer (world position, view-space normal, alpha
 * mask) and computes per-pixel ambient occlusion via hemisphere sampling.
 * The result is written to the SSAO attachment (R8_UNORM) for consumption
 * by the lighting pass.
 *
 * Architecture:
 * - Inherits from ComputePass for shared infrastructure (sampler, descriptor
 *   pool/sets, barrier transitions, dispatch logic).
 * - Owns the compute pipeline, camera/kernel UBO, and noise UBO.
 * - Borrows RenderCache for G-Buffer and SSAO image views.
 *
 * @note Hemisphere sampling with 16 kernel samples, world-space depth
 *       comparison, and lightweight 2-pixel neighbour blur.
 */

#pragma once

#include "passes/ComputePass.h"
#include "../VulkanBuffer.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <memory>
#include <vector>

namespace neurus {

// --- Forward declarations ---
class RenderCache;

// ---------------------------------------------------------------------------
// GPU-side data structures (std140-compatible)
// ---------------------------------------------------------------------------

/**
 * @brief Single kernel sample (tangent-space direction, std140 as vec4).
 *
 * The kernel is generated on CPU as hemisphere samples biased toward
 * the normal (z ∈ [0, 1]).  Each entry is padded to 16 bytes for UBO.
 */
struct alignas(16) KernelSampleGpu
{
	float x, y, z;   ///< Tangent-space direction (unit hemisphere)
	float _pad;       ///< std140 padding to 16 bytes
};
static_assert(sizeof(KernelSampleGpu) == 16, "KernelSampleGpu must be 16 bytes (std140)");

/**
 * @brief SSAO parameters UBO — camera matrices + kernel (updated per frame).
 *
 * Layout (std140):
 *   mat4  viewProj       offset 0   (64 bytes)
 *   mat4  view           offset 64  (64 bytes)
 *   vec4  cameraPos      offset 128 (16 bytes, w unused)
 *   KernelSampleGpu[16]  offset 144 (256 bytes)
 *   Total: 400 bytes.
 */
struct alignas(16) SSAOParamsGpu
{
	float  viewProj[16];                        ///< projection * view (column-major)
	float  view[16];                            ///< view matrix (column-major)
	float  camX, camY, camZ, camW;              ///< camera world-space position + pad
	KernelSampleGpu kernelSamples[16];          ///< Hemisphere kernel samples
};
static_assert(sizeof(SSAOParamsGpu) == 400, "SSAOParamsGpu must be 400 bytes (std140)");

/**
 * @brief Noise rotation entry (16 bytes, std140).
 *
 * The noise buffer contains 16×16 = 256 random rotation vectors that
 * randomise the tangent-plane orientation per pixel, hiding banding
 * artefacts from the regular hemisphere sampling pattern.
 */
struct alignas(16) NoiseEntryGpu
{
	float x, y, z;   ///< Random rotation direction
	float _pad;       ///< std140 padding to 16 bytes
};
static_assert(sizeof(NoiseEntryGpu) == 16, "NoiseEntryGpu must be 16 bytes (std140)");

// ---------------------------------------------------------------------------
// SSAOPass
// ---------------------------------------------------------------------------

/**
 * @brief Screen-space ambient occlusion compute pass.
 *
 * Reads the G-Buffer (Position, Normal, Albedo) as combined image samplers,
 * performs hemisphere sampling in world-space, applies a lightweight
 * neighbour blur, and writes the occlusion factor (R8) to the SSAO attachment.
 *
 * Inherits shared compute-pass infrastructure from ComputePass.
 */
class SSAOPass : public ComputePass
{
public:
	/** Default kernel size and noise dimensions. */
	static constexpr uint32_t kDefaultKernelLength = 16;
	static constexpr uint32_t kNoiseSize           = 16;
	static constexpr uint32_t kNoiseEntryCount     = kNoiseSize * kNoiseSize;  // 256
	static constexpr uint32_t kMaxKernelSamples    = 16;

	/**
	 * @brief Constructs the SSAO pass and creates all GPU resources.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for sampler creation).
	 * @param numSets           Number of descriptor sets (one per in-flight frame).
	 * @param graphicsQueue     Graphics queue for noise UBO staging upload.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 * @param compSpv           Embedded compute shader SPIR-V data.
	 * @param compSize          Compute shader SPIR-V size in bytes.
	 *
	 * @throws std::runtime_error if shader or pipeline creation fails.
	 */
	SSAOPass(const vk::raii::Device& device,
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
	 * @brief Records the SSAO compute dispatch into a command buffer.
	 *
	 *   1. Writes G-Buffer + SSAO descriptors for this frame slot.
	 *   2. Transitions G-Buffer images to SHADER_READ_ONLY_OPTIMAL.
	 *   3. Transitions SSAO attachment to GENERAL for compute write.
	 *   4. Binds pipeline, descriptor set, push constants.
	 *   5. Dispatches ceil(width/16) × ceil(height/16) × 1 thread groups.
	 *   6. Inserts a memory barrier for SSAO output visibility.
	 *
	 * @param cmdBuf  Command buffer in recording state.
	 * @param ctx     Per-frame context (render extent, frame index).
	 */
	void Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx) override;

	/**
	 * @brief Writes all descriptors (image + buffer) into the specified set.
	 * @param setIndex  Index into m_descriptorSets (0 … numSets-1).
	 */
	void WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache) override;

private:
	/**
	 * @brief Creates the descriptor set layout (6 bindings).
	 *
	 * Bindings:
	 *   0: gPosition       (combined image sampler)
	 *   1: gNormal          (combined image sampler)
	 *   2: gAlbedo          (combined image sampler)
	 *   3: outputSSAO       (storage image, R8)
	 *   4: SSAOParams       (uniform buffer)
	 *   5: NoiseBuffer      (uniform buffer)
	 */
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);

	/**
	 * @brief Creates the compute pipeline via ComputePipelineBuilder.
	 */
	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device,
	                                  const uint32_t* compSpv,
	                                  size_t compSize);

	/**
	 * @brief Generates the hemisphere kernel samples.
	 *
	 * Creates 16 tangent-space directions on the unit hemisphere,
	 * biased toward the normal (z > 0).  Samples are distributed with
	 * increasing radius (near samples cluster close to the centre,
	 * far samples explore the periphery).
	 *
	 * @return Array of 16 kernel samples in std140-compatible layout.
	 */
	static std::array<KernelSampleGpu, kMaxKernelSamples> GenerateKernel();

	/**
	 * @brief Generates 256 random rotation vectors for the noise buffer.
	 *
	 * Each entry is a random direction on the unit sphere used to
	 * randomise the tangent-plane orientation per pixel.
	 *
	 * @return Array of 256 noise entries in std140-compatible layout.
	 */
	static std::array<NoiseEntryGpu, kNoiseEntryCount> GenerateNoise();

	// --- Pipeline ---
	vk::raii::Pipeline m_pipeline;

	// --- Owned UBOs ---
	std::unique_ptr<VulkanBuffer> m_paramsUBO;   ///< SSAO params (camera + kernel), host-visible
	std::unique_ptr<VulkanBuffer> m_noiseUBO;    ///< Noise rotation vectors, device-local

	// --- Push constant data ---
	int32_t m_kernelLength = kDefaultKernelLength;
	float   m_radius       = 0.15f;
	int32_t m_noiseSize    = kNoiseSize;
};

} // namespace neurus
