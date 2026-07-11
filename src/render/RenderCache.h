#pragma once

#include "Image.h"
#include "resources/MeshGPU.h"
#include "resources/EnvironmentGPU.h"
#include "resources/LightGPU.h"
#include "resources/LightingGPU.h"

#include <vulkan/vulkan_raii.hpp>

#include <unordered_map>
#include <variant>
#include <vector>

namespace neurus {

/**
 * @brief Named attachment identifiers for G-Buffer and post-FX framebuffer attachments.
 */
enum class AttachmentName
{
	// --- G-Buffer ---
	Position,           ///< World-space position (RGBA16_SFLOAT)
	Normal,             ///< World-space normal (RGBA16_SFLOAT)
	Albedo,             ///< Base color (RGBA8_SRGB)
	MetallicRoughness,  ///< Packed metallic + roughness (RGBA8_UNORM)
	Depth,              ///< Depth attachment (D32_SFLOAT)

	// --- Post-FX ---
	HDRColor,           ///< HDR color output (RGBA16F)
	SSAO,               ///< Screen-space ambient occlusion (R8)
	SSR,                ///< Screen-space reflections (RGBA16F)

	// --- Shadow ---
	ShadowDepth,        ///< Point light shadow depth cubemap (D32_SFLOAT, eCube)

	// Count sentinel
	Count,
};

/**
 * @brief Cross-frame resource pool for G-Buffer and post-FX framebuffer attachments.
 *
 * Each attachment is an Image with a preconfigured format and usage flags.
 * Attachments are created lazily via GetAttachment(name, extent) on first access.
 *
 * Non-copyable, movable.
 *
 * @note Images are created with ImageState::Undefined CPU tracking.
 *       Callers that need a specific layout should use Barrier::Transition().
 */
class RenderCache
{
public:
	/**
	 * @brief Constructs the render cache and initializes LightingGPU.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (retained reference, used for format/memory queries).
	 * @param graphicsQueue     Graphics queue for staging uploads.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 */
	RenderCache(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice);

	/**
	 * @brief Initialises the LightingGPU with staging queue info.
	 *
	 * Must be called after construction and before any UpdateLighting() calls.
	 * Creates the internal LightingGPU and configures its staging queue.
	 *
	 * @param graphicsQueue     Graphics queue for SSBO staging uploads.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 */
	void InitLightingGPU(vk::Queue graphicsQueue, uint32_t queueFamilyIndex);

	~RenderCache() = default;

	// --- Non-copyable, movable ---
	RenderCache(const RenderCache&) = delete;
	RenderCache& operator=(const RenderCache&) = delete;
	RenderCache(RenderCache&&) noexcept = default;
	RenderCache& operator=(RenderCache&&) noexcept = default;

	/**
	 * @brief Returns (or lazily creates) the Image for a named attachment.
	 *
	 * If the attachment does not exist, it is created at the given extent.
	 * If it already exists, the extent parameter is ignored and the existing
	 * attachment is returned unchanged. Call CleanScreenSpace() first to
	 * force re-creation at a new extent.
	 *
	 * Newly created images start in ImageState::Undefined.
	 * Use Barrier::Transition() to move them to a usable state.
	 *
	 * @param name   Attachment identifier.
	 * @param extent Image dimensions for lazy creation.
	 * @return Non-owning reference to the attachment image.
	 */
	Image& GetAttachment(AttachmentName name, vk::Extent2D extent);

	// --- Per-light shadow resources (lazily created) ---

	/// Maximum number of shadow-casting lights (array layers in the intensity image).
	static constexpr uint32_t MAX_SHADOW_LAYERS = 4;

	// --- Mesh GPU resources (lazily created) ---

	/**
	 * @brief Returns the cached MeshGPU for an object, or nullptr.
	 *
	 * Read-only lookup - does NOT create.  Used for per-frame query
	 * of previously-uploaded mesh GPU resources.
	 *
	 * @param objectId Unique object ID of the mesh.
	 * @return Non-owning pointer, or nullptr if not found.
	 */
	MeshGPU* GetMeshGPU(int objectId);

	/** @brief const overload of GetMeshGPU(). */
	const MeshGPU* GetMeshGPU(int objectId) const;

	/**
	 * @brief Returns (or lazily creates) the shadow intensity image array.
	 *
	 * A single 2D_ARRAY image (R8_UNORM, MAX_SHADOW_LAYERS layers) shared
	 * by all shadow-casting point lights.  Each light gets a unique layer
	 * index via GetShadowIntensityLayer().
	 *
	 * @param extent Image dimensions (should match the current render extent).
	 * @return Non-owning reference to the layered intensity Image.
	 */
	Image& GetShadowIntensityArray(vk::Extent2D extent);

	/**
	 * @brief Returns (or allocates) the layer index for a light's shadow intensity.
	 *
	 * On first call for a lightUID, allocates the next available layer (0, 1, 2…).
	 * The array must have been created first via GetShadowIntensityArray().
	 * Asserts if MAX_SHADOW_LAYERS is exceeded.
	 *
	 * @param lightUID Unique identifier of the point light (int).
	 * @param extent   Image dimensions (must match GetShadowIntensityArray's extent).
	 * @return Layer index (0 … MAX_SHADOW_LAYERS-1).
	 */
	uint32_t GetShadowIntensityLayer(int lightUID, vk::Extent2D extent);

	/**
	 * @brief Returns the shadow intensity layer index for a light (lookup only, no allocation).
	 *
	 * @param lightUID Unique identifier of the point light (int).
	 * @return Layer index, or 0 if not found.
	 */
	uint32_t GetShadowIntensityLayerIndex(int lightUID) const;

	/**
	 * @brief Returns all light UIDs that currently have LightGPU entries.
	 *
	 * Iterates rc_lightGPUs keys. Returns an empty vector if no entries exist.
	 *
	 * @return Vector of light UID integers.
	 */
	std::vector<int> GetShadowMapUIDs() const;

	/**
	 * @brief Returns the shadow intensity array image, or nullptr if not yet created.
	 *
	 * @return Non-owning pointer, or nullptr.
	 */
	Image* GetShadowIntensityArray() const;

	/**
	 * @brief Removes all per-light shadow resources for the given light.
	 *
	 * Recycles the intensity layer index and clears shadow maps
	 * within the LightGPU for this lightUID.
	 * Safe to call for lights that have no resources yet.
	 *
	 * @param lightUID Unique identifier of the point light (int).
	 */
	void RemoveLight(int lightUID);

	// --- Per-light GPU resources (registration/query) ---

	/**
	 * @brief Register a LightGPU into the cache.
	 * Takes ownership via move semantics.  Overwrites any existing
	 * entry for the same lightUID.
	 * @param lightUID Unique light identifier.
	 * @param lightGPU Constructed LightGPU to cache (moved in).
	 */
	void UseLightGPU(int lightUID, LightGPU lightGPU);

	/**
	 * @brief Returns the cached LightGPU for a light UID, or nullptr.
	 * @param lightUID Unique light identifier.
	 * @return Non-owning pointer, or nullptr if not found.
	 */
	LightGPU* GetLightGPU(int lightUID);

	/** @brief const overload of GetLightGPU(). */
	const LightGPU* GetLightGPU(int lightUID) const;

	/**
	 * @brief Removes the cached LightGPU for a given light UID.
	 * Safe to call for lights with no GPU resources yet.
	 * @param lightUID Light UID to remove.
	 */
	void RemoveLightGPU(int lightUID);

	/** @brief const overload of GetAttachment() - returns existing only, throws if missing. */
	const Image& GetAttachment(AttachmentName name) const;

	/**
	 * @brief Checks whether the specified attachment has been created.
	 *
	 * @param name Attachment identifier.
	 * @return true if the attachment exists.
	 */
	bool HasAttachment(AttachmentName name) const;

	/**
	 * @brief Full teardown of ALL cached resources.
	 *
	 * Clears every attachment, shadow cubemap, and shadow intensity
	 * entry.  Equivalent to a fresh RenderCache.
	 *
	 * @note Call on application shutdown or when the device is lost.
	 */
	void Clean();

	/**
	 * @brief Clear screen-space attachments (G-Buffer) and shadow intensities.
	 *
	 * Preserves LightGPU-owned shadow maps which retain their fixed
	 * resolution. Screen-space attachments (Position through SSR in
	 * rc_attachments) and per-pixel shadow intensities are discarded
	 * and must be re-created on the next frame.
	 *
	 * @note Call on swapchain resize (screen-space images change size,
	 *       shadow cubemaps do not).
	 */
	/**
	 * @brief Removes the cached MeshGPU for a given object ID.
	 * Safe to call for objects with no MeshGPU yet.
	 * @param objectId Object ID to remove.
	 */
	void RemoveMeshGPU(int objectId);

	/**
	 * @brief Register a previously-constructed MeshGPU into the cache.
	 * Takes ownership via move semantics.  Overwrites any existing
	 * entry for the same objectId.
	 * @param objectId  Unique object identifier.
	 * @param meshGPU   Constructed MeshGPU to cache (moved in).
	 */
	void UseMeshGPU(int objectId, MeshGPU meshGPU);

	// --- Environment GPU resources (registration/query) ---

	/**
	 * @brief Register a previously-constructed EnvironmentGPU into the cache.
	 * Takes ownership via move semantics.  Overwrites any existing
	 * entry for the same envId.
	 * @param envId   Unique environment identifier.
	 * @param envGPU  Constructed EnvironmentGPU to cache (moved in).
	 */
	void UseEnvironmentGPU(int envId, EnvironmentGPU envGPU);

	/**
	 * @brief Returns the cached EnvironmentGPU for an environment, or nullptr.
	 *
	 * Read-only lookup - does NOT create.  Used by LightingPass for per-frame
	 * IBL descriptor binding.
	 *
	 * @param envId Unique object ID of the environment.
	 * @return Non-owning pointer, or nullptr if not yet created.
	 */
	EnvironmentGPU* GetEnvironmentGPU(int envId);

	/** @brief const overload of GetEnvironmentGPU(). */
	const EnvironmentGPU* GetEnvironmentGPU(int envId) const;

	/**
	 * @brief Removes the cached EnvironmentGPU for a given env ID.
	 * Safe to call for environments with no GPU resources yet.
	 * @param envId Environment object ID to remove.
	 */
	void RemoveEnvironmentGPU(int envId);

	// --- Lighting GPU resources (SSBO owner) ---

	/**
	 * @brief Returns the cached LightingGPU, or nullptr if not yet initialized.
	 * @return Non-owning pointer, or nullptr.
	 */
	LightingGPU* GetLightingGPU();

	/** @brief const overload of GetLightingGPU(). */
	const LightingGPU* GetLightingGPU() const;

	/**
	 * @brief Updates the point light and sun light SSBOs from pre-converted GPU structs.
	 *
	 * Asserts that InitLightingGPU() has been called first.
	 * When a vector is empty, the corresponding SSBO is released.
	 *
	 * @param pointLights  Pre-converted point light GPU structs.
	 * @param sunLights    Pre-converted sun light GPU structs.
	 */
	void UpdateLighting(const std::vector<PointLightStruct>& pointLights,
	                    const std::vector<SunLightStruct>& sunLights);

	/**
	 * @brief Updates the point light and sun light SSBOs from a variant-based light dict.
	 *
	 * Uses std::holds_alternative<> to separate PointLightStruct and SunLightStruct
	 * entries.  Assigns sequential SSBO indices (point lights first, then sun lights)
	 * and assigns shadow map indices (shared pool, point first then sun, max 4).
	 *
	 * Stores uid→index mappings for later lookup via GetShadowIndex().
	 *
	 * @param lightDict  Map: light UID → variant<PointLightStruct, SunLightStruct>.
	 */
	void UpdateLighting(const std::unordered_map<int,
	                    std::variant<PointLightStruct, SunLightStruct>>& lightDict);

	/**
	 * @brief Returns the shadow map index for a light UID.
	 *
	 * Looks up the UID in the shadow index map populated by
	 * UpdateLighting(variant).  Returns 0 if the UID is not found.
	 *
	 * @param lightUID  Unique light identifier.
	 * @return Shadow map index (0-based), or 0 if not found.
	 */
	uint32_t GetShadowIndex(int lightUID) const;

	void CleanScreenSpace();

private:
	/** @brief Configuration record for each attachment type. */
	struct AttachmentConfig
	{
		vk::Format format;
		vk::ImageUsageFlags usage;
		Image::ImageType imageType;
	};

	/** @brief Returns the preconfigured format, usage, and type for a named attachment. */
	static AttachmentConfig ConfigFor(AttachmentName name);

	/** @brief Creates a single attachment at the given extent and inserts it into the map. */
	void createAttachment(AttachmentName name, vk::Extent2D extent);

	// --- References (non-owning) ---
	const vk::raii::Device* rc_device;
	const vk::raii::PhysicalDevice* rc_physicalDevice;

	// --- State ---
	std::unordered_map<AttachmentName, Image> rc_attachments;

	std::unique_ptr<Image> rc_shadowIntensityArray;
	std::unordered_map<int, uint32_t> rc_shadowIntensityLayerIndex;

	// --- Per-mesh lazy GPU resources (key = object UID as int) ---
	std::unordered_map<int, MeshGPU> rc_meshGPUs;

	// --- Per-environment lazy GPU resources (key = environment UID as int) ---
	std::unordered_map<int, EnvironmentGPU> rc_environmentGPUs;

	// --- Per-light GPU resources (key = light UID as int) ---
	std::unordered_map<int, LightGPU> rc_lightGPUs;

	// --- Lighting SSBO storage (owned) ---
	std::unique_ptr<LightingGPU> rc_lightingGPU;

	// --- Light UID → SSBO index / shadow index maps (populated by variant UpdateLighting) ---
	std::unordered_map<int, uint32_t> rc_uidToPointLightIndex;
	std::unordered_map<int, uint32_t> rc_uidToSunLightIndex;
	std::unordered_map<int, uint32_t> rc_uidToShadowIndex;
};

/**
 * @brief Converts an AttachmentName to its string representation.
 * @param name Attachment identifier.
 * @return String name (e.g., "Position", "Normal", "Depth").
 */
const char* AttachmentNameToString(AttachmentName name);

} // namespace neurus
