/**
 * @file LightingCache.h
 * @brief GPU-side light SSBO storage and push constants.
 *
 * LightingCache manages point light and sun light SSBOs (storage buffers)
 * and defines the std140-compatible GPU data structures used by the
 * PBR lighting compute shader.  Separated from LightingPass so the
 * SSBO lifecycle can be managed independently of the compute pass.
 *
 * The structs defined here (PointLightStruct, SunLightStruct,
 * LightingPushConstants) are byte-for-byte replacements for the
 * original PointLightGpu / SunLightGpu types in LightingPass.h.
 * Renamed to avoid collision with the scene-layer LightGPU struct.
 */

#pragma once

#include "../buffers/ArrayBuffer.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
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
 *   int  transEnabled   offset 100 (4 bytes — transparent background)
 *          padding     offset 104 (8 bytes, aligns mat4 to 16)
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
	int32_t  transEnabled;          ///< Transparent background flag (0 = opaque, 1 = transparent)
	float    _pad2[2];              ///< Padding to align invProjView at offset 112 (16-byte alignment)
	float    invProjView[16];       ///< Inverse of (projection * view) matrix for skybox ray
};
static_assert(sizeof(LightingPushConstants) == 176, "LightingPushConstants must be 176 bytes");

// ---------------------------------------------------------------------------
// LightingCache
// ---------------------------------------------------------------------------

/**
 * @brief Manages light SSBO storage for the PBR lighting pipeline.
 *
 * Owns ArrayBuffer instances for point light and sun light arrays.
 * UpdatePointLights() / UpdateSunLights() accept pre-sorted vectors with
 * shadowMapIndex already stamped by the caller (RenderCache).
 *
 * UpdatePointLight(index) / UpdateSunLight(index) provide single-element
 * updates via ArrayBuffer::Update() — used by RenderCache for per-light
 * property changes without a full SSBO rebuild.
 *
 * Non-copyable.  All borrowed Vulkan handles must outlive this object.
 */
class LightingCache
{
public:
	/**
	 * @brief Constructs the light GPU storage manager.
	 *
	 * The underlying GPU buffers are allocated lazily on the first upload.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (for buffer memory queries).
	 * @param graphicsQueue     Graphics queue for staging uploads.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 */
	LightingCache(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice,
	            vk::Queue graphicsQueue,
	            uint32_t queueFamilyIndex);

	// Non-copyable — owns GPU resources (reference members prevent move-assign)
	LightingCache(const LightingCache&) = delete;
	LightingCache& operator=(const LightingCache&) = delete;

	// Movable
	LightingCache(LightingCache&&) noexcept = default;

	// --- Full SSBO rebuild ---

	/**
	 * @brief Creates or updates the point light SSBO from a pre-built vector.
	 *
	 * The vector must already be sorted and have shadowMapIndex fields
	 * populated by the caller.  An empty vector releases the buffer.
	 *
	 * @param lights  Point light GPU structs (sorted, shadow indices set).
	 */
	void UpdatePointLights(const std::vector<PointLightStruct>& lights);

	/**
	 * @brief Creates or updates the sun light SSBO from a pre-built vector.
	 *
	 * Same semantics as UpdatePointLights().
	 *
	 * @param lights  Sun light GPU structs (sorted, shadow indices set).
	 */
	void UpdateSunLights(const std::vector<SunLightStruct>& lights);

	// --- Per-light update (single element, no full rebuild) ---

	/**
	 * @brief Overwrites a single point light element at the given SSBO index.
	 *
	 * Uses ArrayBuffer::Update() — no full re-upload.  The caller
	 * (RenderCache) is responsible for providing the correct index and
	 * a struct with shadowMapIndex already stamped.
	 *
	 * @param light  Updated point light struct.
	 * @param index  Zero-based SSBO element index.
	 */
	void UpdatePointLight(const PointLightStruct& light, uint32_t index);

	/**
	 * @brief Overwrites a single sun light element at the given SSBO index.
	 *
	 * Same semantics as UpdatePointLight().
	 *
	 * @param light  Updated sun light struct.
	 * @param index  Zero-based SSBO element index.
	 */
	void UpdateSunLight(const SunLightStruct& light, uint32_t index);

	// --- Accessors ---

	/** @brief Returns the point light SSBO, or nullptr when no lights exist. */
	const GPUBuffer* GetPointLightSSBO() const;

	/** @brief Returns the sun light SSBO, or nullptr when no sun lights exist. */
	const GPUBuffer* GetSunLightSSBO() const;

	/** @brief Number of point lights in the SSBO. */
	uint32_t GetPointLightCount() const;

	/** @brief Number of sun lights in the SSBO. */
	uint32_t GetSunLightCount() const;

private:
	// --- Owned GPU buffers ---
	ArrayBuffer<PointLightStruct> m_pointLightSSBO;
	ArrayBuffer<SunLightStruct> m_sunLightSSBO;
};

} // namespace neurus
