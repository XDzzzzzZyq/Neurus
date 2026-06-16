#pragma once

#include "VulkanImage.h"

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
};

/**
 * @brief Manages G-Buffer and post-FX framebuffer attachments as VulkanImage instances.
 *
 * Each attachment is a VulkanImage with a preconfigured format and usage flags.
 * Attachments are created via Create() and can be resized via Resize().
 * Individual attachments are accessed by their AttachmentName.
 *
 * Non-copyable, movable.
 *
 * @note Uses VK_IMAGE_LAYOUT_UNDEFINED as the initial layout for all images.
 *       Layout transitions are performed later by the render pass (T22).
 */
class AttachmentManager
{
public:
	/**
	 * @brief Constructs the attachment manager.
	 *
	 * @param device         Logical device (retained reference).
	 * @param physicalDevice Physical device (retained reference, used for format/memory queries).
	 */
	AttachmentManager(const vk::raii::Device& device,
	                  const vk::raii::PhysicalDevice& physicalDevice);

	~AttachmentManager() = default;

	// --- Non-copyable, movable ---
	AttachmentManager(const AttachmentManager&) = delete;
	AttachmentManager& operator=(const AttachmentManager&) = delete;
	AttachmentManager(AttachmentManager&&) noexcept = default;
	AttachmentManager& operator=(AttachmentManager&&) noexcept = default;

	/**
	 * @brief Creates all G-Buffer and post-FX attachments at the given extent.
	 *
	 * Replaces any previously created attachments.
	 *
	 * @param extent Image dimensions for all attachments.
	 */
	void Create(vk::Extent2D extent);

	/**
	 * @brief Resizes all existing attachments to a new extent.
	 *
	 * Equivalent to Create(newExtent). All previous image data is discarded.
	 *
	 * @param extent New image dimensions for all attachments.
	 */
	void Resize(vk::Extent2D extent);

	/**
	 * @brief Returns the VulkanImage for a named attachment.
	 *
	 * @param name Attachment identifier.
	 * @return Non-owning reference to the attachment image.
	 * @throws std::out_of_range if the attachment has not been created.
	 */
	VulkanImage& GetAttachment(AttachmentName name);

	/** @brief const overload of GetAttachment(). */
	const VulkanImage& GetAttachment(AttachmentName name) const;

	/**
	 * @brief Checks whether the specified attachment has been created.
	 *
	 * @param name Attachment identifier.
	 * @return true if the attachment exists.
	 */
	bool HasAttachment(AttachmentName name) const;

private:
	/** @brief Configuration record for each attachment type. */
	struct AttachmentConfig
	{
		vk::Format format;
		vk::ImageUsageFlags usage;
		VulkanImage::ImageType imageType;
	};

	/** @brief Returns the preconfigured format, usage, and type for a named attachment. */
	static AttachmentConfig ConfigFor(AttachmentName name);

	/** @brief Creates a single attachment and inserts it into the map. */
	void createAttachment(AttachmentName name);

	// --- References (non-owning) ---
	const vk::raii::Device* m_device;
	const vk::raii::PhysicalDevice* m_physicalDevice;

	// --- State ---
	vk::Extent2D m_extent{};
	std::unordered_map<AttachmentName, VulkanImage> m_attachments;
};

/**
 * @brief Converts an AttachmentName to its string representation.
 * @param name Attachment identifier.
 * @return String name (e.g., "Position", "Normal", "Depth").
 */
const char* AttachmentNameToString(AttachmentName name);

} // namespace neurus
