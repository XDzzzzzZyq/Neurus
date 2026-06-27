#pragma once

#include "Image.h"

#include <vulkan/vulkan_raii.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

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
	ShadowIntensity,    ///< Per-pixel shadow intensity (R8_UNORM)

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

	/**
	 * @brief Returns (or lazily creates) the shadow depth cubemap for a light.
	 *
	 * The cubemap is 1024x1024 D32_SFLOAT with eDepthStencilAttachment | eSampled
	 * usage.  Created on first access for the given lightUID.
	 *
	 * @param lightUID Unique identifier of the point light (int).
	 * @return Non-owning reference to the shadow cubemap Image.
	 */
	Image& GetShadowMap(int lightUID);

	/**
	 * @brief Returns (or lazily creates) the shadow intensity map for a light.
	 *
	 * The map is a screen-space R8_UNORM image with eStorage | eSampled | eTransferSrc
	 * usage.  Created on first access for the given lightUID.  Resolution is provided
	 * by the caller and should match the current render extent.
	 *
	 * @param lightUID Unique identifier of the point light (int).
	 * @param extent   Image dimensions for the intensity map (should match render extent).
	 * @return Non-owning reference to the shadow intensity Image.
	 */
	Image& GetShadowIntensity(int lightUID, vk::Extent2D extent);

	/**
	 * @brief Removes all per-light shadow resources for the given light.
	 *
	 * Erases entries from both m_shadowMaps and m_shadowIntensities.
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
	 * Preserves shadow cubemaps (m_shadowMaps) which are owned by
	 * this RenderCache and retain their fixed 1024×1024 resolution.
	 * Screen-space attachments (Position through SSR in m_attachments)
	 * and per-pixel shadow intensities are discarded and must be
	 * re-created on the next frame.
	 *
	 * @note Call on swapchain resize (screen-space images change size,
	 *       shadow cubemaps do not).
	 */
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
	const vk::raii::Device* m_device;
	const vk::raii::PhysicalDevice* m_physicalDevice;

	// --- State ---
	std::unordered_map<AttachmentName, Image> m_attachments;

	// --- Per-light lazy resources (key = light UID as int) ---
	std::unordered_map<int, Image> m_shadowMaps;
	std::unordered_map<int, Image> m_shadowIntensities;
};

/**
 * @brief Converts an AttachmentName to its string representation.
 * @param name Attachment identifier.
 * @return String name (e.g., "Position", "Normal", "Depth").
 */
const char* AttachmentNameToString(AttachmentName name);

} // namespace neurus
