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
 *   and light SSBO (VulkanBuffer).
 * - Borrows AttachmentManager for G-Buffer and HDR colour image views.
 * - Optionally accepts IBL cubemap resources via SetIBLResources().
 * - Uses ComputePipelineBuilder for pipeline construction.
 *
 * @note Direct lighting + IBL (diffuse + specular).
 * @note Descriptor set layout: 9 bindings (5 sampled images, 1 storage image, 1 SSBO, 2 cube samplers).
 */

#pragma once

#include "DescriptorManager.h"
#include "VulkanBuffer.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

// --- Forward declarations ---
class AttachmentManager;
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
 * Layout:
 *   int  lightCount  offset 0  (4 bytes)
 *   vec4 cameraPos   offset 16 (16 bytes)
 *   mat4 view        offset 32 (64 bytes)
 *   int  iblEnabled  offset 96 (4 bytes)
 *   Total: 100 bytes. Must NOT use alignas — push constant size must match exactly.
 */
struct LightingPushConstants
{
	int32_t  lightCount;            ///< Number of active point lights in SSBO
	float    _pad0[3];              ///< Padding to align cameraPos at offset 16
	float    camX, camY, camZ;      ///< Camera world-space position
	float    _pad1;                 ///< Padding (vec4 → 16 bytes)
	float    view[16];              ///< View matrix (for normal transform VS→WS)
	int32_t  iblEnabled;            ///< IBL enabled flag (0 = disabled, 1 = enabled)
};
static_assert(sizeof(LightingPushConstants) == 100, "LightingPushConstants must be 100 bytes");

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
class LightingPass
{
public:
	/**
	 * @brief Constructs the lighting pass and creates all GPU resources.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for sampler creation).
	 * @param attachmentManager G-Buffer and HDR colour attachment provider (borrowed).
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
	             AttachmentManager& attachmentManager,
	             uint32_t numSets,
	             vk::Queue graphicsQueue,
	             uint32_t queueFamilyIndex,
	             const uint32_t* compSpv,
	             size_t compSize);

	~LightingPass();

	// --- Non-copyable, movable ---
	LightingPass(const LightingPass&) = delete;
	LightingPass& operator=(const LightingPass&) = delete;
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
	 * If the scene has no point lights, a fallback single-element SSBO
	 * is kept so that the descriptor binding remains valid. GetLightCount()
	 * returns 0 in that case.
	 *
	 * @param scene Scene containing the light list.
	 */
	void UploadLights(const Scene& scene);

	/**
	 * @brief Returns the light SSBO (always valid, never nullptr).
	 * @return Non-owning pointer to VulkanBuffer.
	 */
	const VulkanBuffer* GetLightSSBO() const;

	/**
	 * @brief Returns the number of point lights in the SSBO.
	 * @return Light count (0 if no lights uploaded).
	 */
	uint32_t GetLightCount() const;

	// -------------------------------------------------------------------
	// IBL resource injection
	// -------------------------------------------------------------------

	/**
	 * @brief Sets the IBL cubemap resources for bindings 7-8.
	 *
	 * Call before the first Record() to enable IBL (diffuse + specular).
	 * If not called, fallback black cubemaps are used (valid but zero contribution).
	 *
	 * @param irradianceView    ImageView for diffuse irradiance cubemap.
	 * @param irradianceSampler Sampler for diffuse cubemap.
	 * @param prefilteredView   ImageView for specular prefiltered cubemap.
	 * @param prefilteredSampler Sampler for specular cubemap.
	 */
	void SetIBLResources(vk::ImageView irradianceView,
	                     vk::Sampler irradianceSampler,
	                     vk::ImageView prefilteredView,
	                     vk::Sampler prefilteredSampler);

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
	 * @param cameraPos       Camera world-space position.
	 * @param viewMatrix      View matrix (for normal VS→WS transform).
	 * @param renderExtent    Render area dimensions.
	 * @param frameIndex      Index into the descriptor-set ring buffer
	 *                        (0 … numSets-1). One set per in-flight frame
	 *                        avoids updating a set while the GPU is reading it.
	 */
	void Record(vk::CommandBuffer cmdBuf,
	            const glm::vec3& cameraPos,
	            const glm::mat4& viewMatrix,
	            vk::Extent2D renderExtent,
	            uint32_t frameIndex);

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
	 *   5: LightBuffer          (storage buffer / SSBO)
	 *   6: U_AO                 (combined image sampler, SSAO occlusion)
	 *   7: U_Irradiance         (combined image sampler, diffuse IBL cubemap)
	 *   8: U_Prefiltered        (combined image sampler, specular IBL cubemap)
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
	 * @brief Writes all descriptors (image + buffer) into the specified set.
	 *
	 * Called every frame during Record(). One set per in-flight frame
	 * prevents updating a set while the GPU is still reading it.
	 *
	 * @param setIndex  Index into m_descriptorSets (0 … numSets-1).
	 */
	void WriteDescriptors(uint32_t setIndex);

	// --- References (non-owning) ---
	const vk::raii::Device* m_device;
	const vk::raii::PhysicalDevice* m_physicalDevice;
	AttachmentManager* m_attachmentManager;

	// --- Queue handles for SSBO creation ---
	vk::Queue m_graphicsQueue;
	uint32_t m_queueFamilyIndex;

	// --- Sampler ---
	vk::raii::Sampler m_sampler;

	// --- Descriptor resources ---
	DescriptorSetLayout m_descriptorSetLayout;
	DescriptorPool m_descriptorPool;
	std::vector<DescriptorSet> m_descriptorSets;  // one per in-flight frame

	// --- Pipeline ---
	std::unique_ptr<ComputePipelineBuilder> m_pipelineBuilder;  // must outlive pipeline
	vk::raii::Pipeline m_pipeline;

	// --- Owned light SSBO ---
	std::unique_ptr<VulkanBuffer> m_lightSSBO;
	uint32_t m_lightCount = 0;
	std::unique_ptr<VulkanBuffer> m_fallbackSSBO;

	// --- IBL cubemap fallback (1×1 black cubemap, valid when no IBL set) ---
	Image* m_fallbackIrradianceCube = nullptr;
	Image* m_fallbackPrefilteredCube = nullptr;
	vk::raii::Sampler m_fallbackCubeSampler = nullptr;
	// IBL resources injected by SetIBLResources() (non-owning handles)
	vk::ImageView m_iblIrradianceView = nullptr;
	vk::Sampler    m_iblIrradianceSampler = nullptr;
	vk::ImageView m_iblPrefilteredView = nullptr;
	vk::Sampler    m_iblPrefilteredSampler = nullptr;
};
} // namespace neurus
