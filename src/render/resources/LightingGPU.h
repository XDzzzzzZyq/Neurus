/**
 * @file LightingGPU.h
 * @brief GPU-side light SSBO storage and push constants.
 *
 * LightingGPU manages point light and sun light SSBOs (storage buffers)
 * and defines the std140-compatible GPU data structures used by the
 * PBR lighting compute shader.  Separated from LightingPass so the
 * SSBO lifecycle can be managed independently of the compute pass
 * (e.g. shared across passes, or owned by DeferredRenderer).
 *
 * The structs defined here (PointLightStruct, SunLightStruct,
 * LightingPushConstants) are byte-for-byte replacements for the
 * original PointLightGpu / SunLightGpu types in LightingPass.h.
 * Renamed to avoid collision with the scene-layer LightGPU struct.
 */

#pragma once

#include "../buffers/GPUBuffer.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace neurus {

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
 *   int32_t shadowMapIndex(offset 40)
 *   float _pad3[2] (offset 44)
 *   Total: 48 bytes (struct is alignas(16)).
 */
struct alignas(16) PointLightStruct
{
	float colorR, colorG, colorB;   ///< RGB colour (linear)
	float power;                     ///< Luminous intensity
	float posX, posY, posZ;         ///< World-space position
	float radius;                    ///< Physical radius
	int32_t shadowMapIndex = -1;     ///< Index into shadow maps array; -1 = no shadow
	float _pad3[3];                 ///< Padding to 48 bytes (16-byte aligned struct)
};
static_assert(sizeof(PointLightStruct) == 48, "PointLightStruct must be 48 bytes (std140)");

/**
 * @brief Sun (directional) light data uploaded to the GPU SSBO.
 *
 * Layout matches the GLSL SunLight struct (std140, 48 bytes per element):
 *   vec3 direction (offset 0,  padded to 16)
 *   float power    (offset 12)
 *   vec3 color     (offset 16, padded to 16)
 *   int32_t shadowMapIndex (offset 28)
 *   float _pad[4]  (offset 32)
 *   Total: 48 bytes (struct is alignas(16)).
 */
struct alignas(16) SunLightStruct
{
	float directionX, directionY, directionZ; ///< Light direction (world-space)
	float power;                               ///< Luminous intensity
	float colorR, colorG, colorB;              ///< RGB colour (linear)
	int32_t shadowMapIndex = -1;               ///< Index into shadow maps array; -1 = no shadow
	float _pad[4];                             ///< Padding to 48 bytes (16-byte aligned struct)
};
static_assert(sizeof(SunLightStruct) == 48, "SunLightStruct must be 48 bytes (std140)");

/**
 * @brief Push constants for the PBR lighting compute shader.
 *
 * Layout (matches GLSL push_constant block with std430 alignment):
 *   int  lightCount    offset 0   (4 bytes)
 *   int  sunLightCount offset 4   (4 bytes — reuses former padding slot)
 *          padding     offset 8   (8 bytes)
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
	int32_t  sunLightCount;         ///< Number of active sun (directional) lights in SSBO
	float    _pad0[2];              ///< Padding to align cameraPos at offset 16
	float    camX, camY, camZ;      ///< Camera world-space position
	float    _pad1;                 ///< Padding (vec4 → 16 bytes)
	float    view[16];              ///< View matrix (for normal transform VS→WS)
	int32_t  iblEnabled;            ///< IBL enabled flag (0 = disabled, 1 = enabled)
	float    _pad2[3];              ///< Padding to align invProjView at offset 112 (16-byte alignment)
	float    invProjView[16];       ///< Inverse of (projection * view) matrix for skybox ray
};
static_assert(sizeof(LightingPushConstants) == 176, "LightingPushConstants must be 176 bytes");

// ---------------------------------------------------------------------------
// LightingGPU
// ---------------------------------------------------------------------------

/**
 * @brief Manages light SSBO storage for the PBR lighting pipeline.
 *
 * Owns device-local GPUBuffers for point light and sun light arrays.
 * UpdatePointLights() / UpdateSunLights() accept pre-converted GPU struct
 * vectors and create/re-create the SSBO as needed.  When a vector is empty
 * the corresponding SSBO is released and the count is set to zero.
 *
 * Non-copyable, movable.  All borrowed Vulkan handles must outlive this
 * object.
 */
class LightingGPU
{
public:
	/**
	 * @brief Constructs the light GPU storage manager.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for buffer memory queries).
	 * @param graphicsQueue     Graphics queue for staging uploads.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 */
	LightingGPU(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice);

	/**
	 * @brief Initialise the staging queue for SSBO uploads.
	 *
	 * Must be called before UpdatePointLights() or UpdateSunLights().
	 * @param graphicsQueue     Graphics queue for staging uploads.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 */
	void Init(vk::Queue graphicsQueue, uint32_t queueFamilyIndex);

	// Non-copyable — owns GPU resources
	LightingGPU(const LightingGPU&) = delete;
	LightingGPU& operator=(const LightingGPU&) = delete;

	// Movable
	LightingGPU(LightingGPU&&) noexcept = default;
	LightingGPU& operator=(LightingGPU&&) noexcept = default;

	// --- Point light SSBO ---

	/**
	 * @brief Creates or updates the point light SSBO from a pre-converted vector.
	 *
	 * If @a lights is empty the SSBO is released and the count is set to zero.
	 * Otherwise a new GPUBuffer is allocated with eStorageBuffer usage and
	 * the data is uploaded via a staging buffer.
	 *
	 * @param lights  Point light GPU structs to upload.
	 */
	void UpdatePointLights(const std::vector<PointLightStruct>& lights);

	/**
	 * @brief Returns the point light SSBO, or nullptr when no lights exist.
	 * @return Non-owning pointer to GPUBuffer, or nullptr.
	 */
	const GPUBuffer* GetPointLightSSBO() const;

	/**
	 * @brief Returns the number of point lights currently in the SSBO.
	 * @return Light count (0 if no lights uploaded).
	 */
	uint32_t GetPointLightCount() const;

	// --- Sun light SSBO ---

	/**
	 * @brief Creates or updates the sun light SSBO from a pre-converted vector.
	 *
	 * If @a lights is empty the SSBO is released and the count is set to zero.
	 * Otherwise a new GPUBuffer is allocated with eStorageBuffer usage and
	 * the data is uploaded via a staging buffer.
	 *
	 * @param lights  Sun light GPU structs to upload.
	 */
	void UpdateSunLights(const std::vector<SunLightStruct>& lights);

	/**
	 * @brief Returns the sun light SSBO, or nullptr when no sun lights exist.
	 * @return Non-owning pointer to GPUBuffer, or nullptr.
	 */
	const GPUBuffer* GetSunLightSSBO() const;

	/**
	 * @brief Returns the number of sun lights currently in the SSBO.
	 * @return Sun light count (0 if no sun lights uploaded).
	 */
	uint32_t GetSunLightCount() const;

private:
	// --- Borrowed Vulkan handles (must outlive this object) ---
	const vk::raii::Device& m_device;
	const vk::raii::PhysicalDevice& m_physicalDevice;
	vk::Queue m_graphicsQueue;
	uint32_t m_queueFamilyIndex;

	// --- Owned light SSBOs ---
	std::unique_ptr<GPUBuffer> m_pointLightSSBO;
	uint32_t m_pointLightCount = 0;

	std::unique_ptr<GPUBuffer> m_sunLightSSBO;
	uint32_t m_sunLightCount = 0;
};

} // namespace neurus
