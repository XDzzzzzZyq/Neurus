#pragma once

#include "Image.h"
#include "resources/MeshGPU.h"
#include "resources/EnvironmentGPU.h"
#include "resources/LightGPU.h"
#include "resources/LightingCache.h"
#include "resources/PipelineCache.h"

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

	// --- ID ---
	IDBuffer,           ///< Per-pixel object ID (R32_UINT)

	// --- Gizmo ---
	GizmoHighlight,     ///< Selected-object edge highlight (R8_UNORM)

	// --- Composite ---
	ComposedOutput,     ///< Final composed output before swapchain blit (RGBA16F)

	// --- Anti-Aliasing ---
	FXAAOutput,         ///< FXAA anti-aliased output (RGBA16F)
	FXAAOffsets,        ///< FXAA edge subpixel offsets (RG16F, 2-channel)

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
	 * @brief Constructs the render cache and initializes LightingCache.
	 *
	 * @param device            Logical device (retained reference).
	 * @param physicalDevice    Physical device (retained reference, used for format/memory queries).
	 * @param graphicsQueue     Graphics queue for staging uploads.
	 * @param queueFamilyIndex  Queue family index for staging command pool.
	 */
	RenderCache(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice);

	/**
	 * @brief Sets the LightingCache (light SSBO storage) for this cache.
	 *
	 * Must be called before any UpdateLighting() calls.  The LightingCache
	 * is typically created by UploadManager::CreateLightingCache() using
	 * the transfer queue so that SSBO staging uploads do not block the
	 * graphics queue.
	 *
	 * @param lightingCache  Fully constructed LightingCache (moved in).
	 */
	void SetLightingCache(std::unique_ptr<LightingCache> lightingCache);

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

	// --- Pipeline cache ---

	/**
	 * @brief Returns the cross-frame pipeline cache.
	 *
	 * Pipelines are keyed by shader UID (int) — created lazily via
	 * UsePipeline and queried via GetPipeline.
	 *
	 * @return Reference to the internal PipelineCache.
	 */
	PipelineCache& GetPipelineCache();

	/**
	 * @brief Returns a cached pipeline by shader UID, or nullptr.
	 *
	 * Read-only lookup — does NOT create.
	 *
	 * @param uid Unique shader/object identifier for the pipeline.
	 * @return Non-owning pointer, or nullptr if not found.
	 */
	Pipeline* GetPipeline(int uid);

	/**
	 * @brief Register a previously-constructed Pipeline into the cache.
	 *
	 * Takes ownership via move semantics.  Overwrites any existing
	 * entry for the same uid.
	 *
	 * @param uid      Unique shader/object identifier.
	 * @param pipeline Fully constructed Pipeline to cache (moved in).
	 */
	void UsePipeline(int uid, Pipeline pipeline);

	/**
	 * @brief Removes the cached Pipeline for a given UID.
	 * Safe to call for UIDs with no pipeline yet.
	 * @param uid Shader/object UID to remove.
	 */
	void RemovePipeline(int uid);

	// --- Lighting GPU resources (SSBO owner) ---

	/**
	 * @brief Returns the cached LightingCache, or nullptr if not yet initialized.
	 * @return Non-owning pointer, or nullptr.
	 */
	LightingCache* GetLightingCache();

	/** @brief const overload of GetLightingCache(). */
	const LightingCache* GetLightingCache() const;

	/**
	 * @brief Updates the point / sun / spot light SSBOs from a variant dict.
	 *
	 * Separates entries by type, sorts by UID, assigns shadow map indices,
	 * and uploads to LightingCache.  Point and spot lights share the
	 * cubemap shadow atlas.  Maintains internal uid→SSBO-index and
	 * uid→shadow-index maps for later per-light updates.
	 *
	 * @param lightDict  Map: light UID → variant<PointLightStruct, SunLightStruct, SpotLightStruct>.
	 */
	void UpdateLighting(const std::unordered_map<int,
	                    std::variant<PointLightStruct, SunLightStruct, SpotLightStruct>>& lightDict);

	/**
	 * @brief Updates a single light in the SSBO without rebuilding the array.
	 *
	 * Looks up @a lightUID in the internal uid→index maps, stamps
	 * shadowMapIndex, and delegates to LightingCache::UpdatePointLight,
	 * UpdateSunLight or UpdateSpotLight.  No-op if the uid is not found
	 * (light added after the last full rebuild — call UpdateLighting first).
	 *
	 * @param lightUID  Unique light identifier.
	 * @param light     Variant containing the updated GPU struct
	 *                  (shadowMapIndex = -1 is OK; it will be restored
	 *                  from the shadow index map).
	 */
	void UpdateLight(int lightUID,
	                 const std::variant<PointLightStruct, SunLightStruct, SpotLightStruct>& light);

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

	// --- Per-mesh lazy GPU resources (key = object UID as int) ---
	std::unordered_map<int, MeshGPU> rc_meshGPUs;

	// --- Per-environment lazy GPU resources (key = environment UID as int) ---
	std::unordered_map<int, EnvironmentGPU> rc_environmentGPUs;

	// --- Per-light GPU resources (key = light UID as int) ---
	std::unordered_map<int, LightGPU> rc_lightGPUs;

	// --- Pipeline cache (keyed by shader UID string) ---
	PipelineCache rc_pipelineCache;

	// --- Lighting SSBO storage (owned) ---
	std::unique_ptr<LightingCache> rc_lightingCache;

	// --- Light UID → SSBO index / shadow index maps (populated by UpdateLighting) ---
	std::unordered_map<int, uint32_t> rc_uidToSSBOIdx;     ///< uid → SSBO element index
	std::unordered_map<int, uint32_t> rc_uidToShadowLayer; ///< uid → shadow intensity layer

};

/**
 * @brief Converts an AttachmentName to its string representation.
 * @param name Attachment identifier.
 * @return String name (e.g., "Position", "Normal", "Depth").
 */
const char* AttachmentNameToString(AttachmentName name);

/**
 * @brief Kind of a graph resource, distinguishing the different ways a
 *        RenderGraph edge's endpoint resource is stored/addressed.
 *
 * Not every graph resource is a single RenderCache attachment: shadow depth
 * is stored per-light in LightGPU (a "bundle"), and shadow intensity is a
 * unified 2D array reached via GetShadowIntensityArray(). ResourceId lets the
 * graph key edges on any of these uniformly.
 */
enum class ResourceKind : uint8_t
{
	Attachment,        ///< A RenderCache attachment addressed by AttachmentName.
	ShadowDepthBundle, ///< Per-light shadow depth maps (LightGPU) — a wiring/ordering token.
	ShadowIntensity,   ///< Unified shadow-intensity 2D array (GetShadowIntensityArray).
};

/**
 * @brief Identity of a resource an edge in the RenderGraph carries.
 *
 * Implicitly convertible from AttachmentName so the common attachment case
 * stays terse (`AttachmentName::Position`). Non-attachment resources use the
 * named factories. Two ResourceIds are equal iff kind and attachment match,
 * and Key() yields a stable string used as the graph's socket name.
 */
struct ResourceId
{
	ResourceKind   kind       = ResourceKind::Attachment;
	AttachmentName attachment = AttachmentName::Position; ///< Meaningful iff kind==Attachment.

	constexpr ResourceId() = default;
	constexpr ResourceId(AttachmentName name) // NOLINT(google-explicit-constructor): intentional
		: kind(ResourceKind::Attachment), attachment(name) {}

	static constexpr ResourceId ShadowDepthBundle()
	{
		ResourceId r; r.kind = ResourceKind::ShadowDepthBundle; return r;
	}
	static constexpr ResourceId ShadowIntensity()
	{
		ResourceId r; r.kind = ResourceKind::ShadowIntensity; return r;
	}

	bool operator==(const ResourceId&) const = default;

	/** @return Stable string key identifying this resource (used as socket name). */
	const char* Key() const
	{
		switch (kind)
		{
		case ResourceKind::Attachment:        return AttachmentNameToString(attachment);
		case ResourceKind::ShadowDepthBundle: return "ShadowDepthBundle";
		case ResourceKind::ShadowIntensity:   return "ShadowIntensity";
		}
		return "Unknown";
	}
};

} // namespace neurus
