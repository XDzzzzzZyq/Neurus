#pragma once

#include "Image.h"
#include "MeshGPU.h"
#include "Texture.h"

#include <vulkan/vulkan_raii.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neurus {

// Forward declarations
enum LightType : int;    // defined in scene/Light.h
class MeshData;           // defined in asset/MeshData.h
class Environment;        // defined in scene/Environment.h
class IBLPass;            // defined in passes/IBLPass.h

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
 * @brief GPU-side IBL environment resources owned by the Renderer layer.
 *
 * Holds diffuse irradiance and specular prefiltered cubemap Textures
 * (Image + Sampler).  Created lazily by RenderCache::CreateEnvironmentGPU()
 * and read per-frame by LightingPass via GetEnvironmentGPU().
 *
 * Non-copyable (GPU resource handles are move-only).
 */
struct EnvironmentGPU
{
	std::unique_ptr<Texture> diffuseTexture;   ///< Diffuse irradiance cubemap (64 px, 1 mip)
	std::unique_ptr<Texture> specularTexture;  ///< Specular prefiltered cubemap (2048 px, 8 mips)
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
	 * @brief Constructs the render cache.
	 *
	 * @param device         Logical device (retained reference).
	 * @param physicalDevice Physical device (retained reference, used for format/memory queries).
	 */
	RenderCache(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice);

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
	 * @brief Returns (or lazily creates) the MeshGPU for a mesh by its object ID.
	 *
	 * On first access for a given objectId, creates VertexBuffer and IndexBuffer
	 * from the MeshData and stores them in a MeshGPU.  Subsequent calls return
	 * the cached MeshGPU immediately.
	 *
	 * @param objectId          Unique object ID of the mesh (from Mesh::GetObjectID()).
	 * @param queue             Graphics queue for staging upload (first call only).
	 * @param queueFamilyIndex  Queue family index for temp command pool.
	 * @param meshData          CPU-side mesh data (vertices + indices).
	 * @return Non-owning reference to the cached MeshGPU.
	 */
	MeshGPU& GetMeshGPU(int objectId,
	                    vk::Queue queue,
	                    uint32_t queueFamilyIndex,
	                    const MeshData& meshData);

	/**
	 * @brief Returns (or lazily creates) the shadow depth image for a light.
	 *
	 * Point lights (default) get a 1024x1024 D32_SFLOAT cubemap with
	 * eDepthStencilAttachment | eSampled | eTransferSrc usage.
	 * Sun lights get a 2048x2048 D32_SFLOAT 2D image with
	 * eDepthStencilAttachment | eSampled usage.
	 * Created on first access for the given lightUID.
	 *
	 * @param lightUID Unique identifier of the light (int).
	 * @param type     Light type (defaults to POINTLIGHT for backward compatibility).
	 * @return Non-owning reference to the shadow depth Image.
	 */
	Image& GetShadowMap(int lightUID, LightType type);

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
	 * @brief Returns (or lazily creates) a colour-format shadow cubemap for a light.
	 *
	 * The cubemap is a screen-res R32G32B32A32_SFLOAT with eColorAttachment | eSampled | eTransferSrc
	 * usage. ShadowDepthPass renders depth encoded as colour here for GPU compatibility where
	 * the depth-only Multiview pipeline does not render correctly.
	 *
	 * @param lightUID Unique identifier of the point light (int).
	 * @param extent   Image dimensions for the cubemap (matches shadow map resolution).
	 * @return Non-owning reference to the colour cubemap Image.
	 */
	Image& GetShadowColorMap(int lightUID, vk::Extent2D extent);

	/**
	 * @brief Returns all light UIDs that currently have shadow cubemaps.
	 *
	 * Iterates rc_shadowMaps keys.  Returns an empty vector if no shadow maps exist.
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
	 * Erases entries from rc_shadowMaps, rc_shadowColorMaps,
	 * and recycles the intensity layer index.
	 * Safe to call for lights that have no resources yet.
	 *
	 * @param lightUID Unique identifier of the point light (int).
	 */
	void RemoveLight(int lightUID);

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
	 * Preserves shadow cubemaps (rc_shadowMaps) which are owned by
	 * this RenderCache and retain their fixed 1024×1024 resolution.
	 * Screen-space attachments (Position through SSR in rc_attachments)
	 * and per-pixel shadow intensities are discarded and must be
	 * re-created on the next frame.
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

	// --- Environment GPU resources (lazily created) ---

	/**
	 * @brief Returns (or lazily creates) the EnvironmentGPU for an environment.
	 *
	 * On first call for a given envId, loads the equirectangular ImageData
	 * from the Environment (or generates a pink-purple fallback), uploads it
	 * to the GPU, creates diffuse (64 px, 1 mip) and specular (2048 px, 8 mips)
	 * cubemap Images with samplers, and runs IBL convolution via IBLPass.
	 *
	 * Subsequent calls return the cached EnvironmentGPU immediately.
	 *
	 * @param envId            Unique object ID of the environment.
	 * @param env              CPU-side environment (provides equirect path + ImageData).
	 * @param queue            Graphics queue for staging upload.
	 * @param queueFamilyIndex Queue family index for transient command pools.
	 * @param iblPass          IBL pass for cubemap convolution.
	 * @return Non-owning reference to the cached EnvironmentGPU.
	 */
	EnvironmentGPU& CreateEnvironmentGPU(int envId,
	                                     const Environment& env,
	                                     vk::Queue queue,
	                                     uint32_t queueFamilyIndex,
	                                     IBLPass& iblPass);

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

	// --- Per-light lazy resources (key = light UID as int) ---
	std::unordered_map<int, Image> rc_shadowMaps;
	std::unique_ptr<Image> rc_shadowIntensityArray;
	std::unordered_map<int, uint32_t> rc_shadowIntensityLayerIndex;
	std::unordered_map<int, Image> rc_shadowColorMaps;

	// --- Per-mesh lazy GPU resources (key = object UID as int) ---
	std::unordered_map<int, MeshGPU> rc_meshGPUs;

	// --- Per-environment lazy GPU resources (key = environment UID as int) ---
	std::unordered_map<int, EnvironmentGPU> rc_environmentGPUs;
};

/**
 * @brief Converts an AttachmentName to its string representation.
 * @param name Attachment identifier.
 * @return String name (e.g., "Position", "Normal", "Depth").
 */
const char* AttachmentNameToString(AttachmentName name);

} // namespace neurus
