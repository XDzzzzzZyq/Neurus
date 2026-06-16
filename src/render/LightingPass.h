/**
 * @file LightingPass.h
 * @brief PBR lighting pass — compute shader reading G-Buffer and evaluating
 *        Cook-Torrance GGX BRDF per point light.
 *
 * LightingPass consumes the G-Buffer attachments written by GeometryPass and
 * evaluates direct PBR lighting into a single HDR colour attachment.
 * Uses a compute shader dispatched at 16×16 thread groups.
 *
 * Architecture:
 * - Owns the compute pipeline, descriptor sets, sampler, and descriptor pool.
 * - Borrows AttachmentManager for G-Buffer and HDR colour image views.
 * - Receives a VulkanBuffer (SSBO) containing PointLightGpu data.
 * - Uses ComputePipelineBuilder for pipeline construction.
 *
 * @note Direct lighting only (no IBL, no shadows).
 * @note Descriptor set layout: 6 bindings (4 sampled images, 1 storage image, 1 SSBO).
 */

#pragma once

#include "DescriptorManager.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

// --- Forward declarations ---
class AttachmentManager;
class ComputePipelineBuilder;
class VulkanBuffer;

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
 *   Total: 48 bytes (aligned to 16).
 */
struct alignas(16) PointLightGpu
{
	float colorR, colorG, colorB;   ///< RGB colour (linear)
	float _pad0;                     ///< std140 padding after vec3
	float posX, posY, posZ;         ///< World-space position
	float _pad1;                     ///< std140 padding after vec3
	float power;                     ///< Luminous intensity
	float radius;                    ///< Physical radius
	float _pad2[2];                  ///< std140 padding to 48 bytes
};
static_assert(sizeof(PointLightGpu) == 48, "PointLightGpu must be 48 bytes (std140)");

/**
 * @brief Push constants for the PBR lighting compute shader.
 *
 * Layout (std140 in push-constant memory):
 *   int  lightCount  offset 0  (4 bytes)
 *   vec4 cameraPos   offset 16 (16 bytes)
 *   mat4 view        offset 32 (64 bytes)
 *   Total: 96 bytes.
 */
struct alignas(16) LightingPushConstants
{
	int32_t  lightCount;            ///< Number of active point lights in SSBO
	float    _pad0[3];              ///< Padding to align cameraPos at offset 16
	float    camX, camY, camZ;      ///< Camera world-space position
	float    _pad1;                 ///< Padding (vec4 → 16 bytes)
	float    view[16];              ///< View matrix (for normal transform VS→WS)
};
static_assert(sizeof(LightingPushConstants) == 96, "LightingPushConstants must be 96 bytes");

// ---------------------------------------------------------------------------
// LightingPass
// ---------------------------------------------------------------------------

/**
 * @brief PBR lighting compute pass.
 *
 * Reads the G-Buffer (Position, Normal, Albedo, MetallicRoughness) as
 * combined image samplers, iterates point lights from an SSBO, evaluates
 * the Cook-Torrance GGX BRDF, and writes HDR colour to the output image.
 *
 * Non-copyable, movable.
 */
class LightingPass
{
public:
	/**
	 * @brief Constructs the lighting pass and creates all GPU resources.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for sampler creation).
	 * @param attachmentManager G-Buffer and HDR colour attachment provider (borrowed).
	 * @param compSpv           Embedded compute shader SPIR-V data.
	 * @param compSize          Compute shader SPIR-V size in bytes.
	 *
	 * @throws std::runtime_error if shader or pipeline creation fails.
	 */
	LightingPass(const vk::raii::Device& device,
	             const vk::raii::PhysicalDevice& physicalDevice,
	             AttachmentManager& attachmentManager,
	             const uint32_t* compSpv,
	             size_t compSize);

	~LightingPass();

	// --- Non-copyable, movable ---
	LightingPass(const LightingPass&) = delete;
	LightingPass& operator=(const LightingPass&) = delete;
	LightingPass(LightingPass&&) noexcept = default;
	LightingPass& operator=(LightingPass&&) noexcept = default;

	/**
	 * @brief Records the PBR lighting compute dispatch into a command buffer.
	 *
	 *   1. Transitions G-Buffer images to SHADER_READ_ONLY_OPTIMAL.
	 *   2. Transitions HDRColor output to GENERAL.
	 *   3. Binds the compute pipeline, descriptor set, and push constants.
	 *   4. Dispatches ceil(width/16) × ceil(height/16) × 1 thread groups.
	 *   5. Inserts a memory barrier to make the output visible.
	 *
	 * @param cmdBuf          Command buffer in recording state.
	 * @param lightSSBO       SSBO containing PointLightGpu data.
	 * @param lightCount      Number of active lights in the SSBO.
	 * @param cameraPos       Camera world-space position.
	 * @param viewMatrix      View matrix (for normal VS→WS transform).
	 * @param renderExtent    Render area dimensions.
	 */
	void Record(vk::CommandBuffer cmdBuf,
	            const VulkanBuffer& lightSSBO,
	            uint32_t lightCount,
	            const glm::vec3& cameraPos,
	            const glm::mat4& viewMatrix,
	            vk::Extent2D renderExtent);

private:
	/**
	 * @brief Creates the descriptor set layout (6 bindings).
	 *
	 * Bindings:
	 *   0: gPosition       (combined image sampler)
	 *   1: gNormal          (combined image sampler)
	 *   2: gAlbedo          (combined image sampler)
	 *   3: gMetallicRoughness (combined image sampler)
	 *   4: outputImage      (storage image)
	 *   5: LightBuffer      (storage buffer / SSBO)
	 */
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);

	/**
	 * @brief Creates a nearest-neighbour sampler for G-Buffer reads.
	 */
	static vk::raii::Sampler CreateSampler(const vk::raii::Device& device,
	                                       const vk::raii::PhysicalDevice& physicalDevice);

	/**
	 * @brief Creates the compute pipeline via ComputePipelineBuilder.
	 */
	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device,
	                                  const uint32_t* compSpv,
	                                  size_t compSize);

	/**
	 * @brief Writes all descriptors (image + buffer) into the allocated set.
	 *
	 * Called once during construction; re-called by RebindImages() after
	 * resize if image views change.
	 */
	void WriteDescriptors(const VulkanBuffer& lightSSBO);

	// --- References (non-owning) ---
	const vk::raii::Device* m_device;
	const vk::raii::PhysicalDevice* m_physicalDevice;
	AttachmentManager* m_attachmentManager;

	// --- Sampler ---
	vk::raii::Sampler m_sampler;

	// --- Descriptor resources ---
	DescriptorSetLayout m_descriptorSetLayout;
	DescriptorPool m_descriptorPool;
	DescriptorSet m_descriptorSet;

	// --- Pipeline ---
	std::unique_ptr<ComputePipelineBuilder> m_pipelineBuilder;  // must outlive pipeline
	vk::raii::Pipeline m_pipeline;
};
} // namespace neurus
