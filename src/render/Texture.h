#pragma once

#include "VulkanImage.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

/**
 * @brief Configuration for the VkSampler created alongside a Texture.
 *
 * Defaults to linear filtering, repeat wrap, and mipmapped sampling.
 */
struct SamplerConfig
{
	vk::Filter magFilter = vk::Filter::eLinear;
	vk::Filter minFilter = vk::Filter::eLinear;
	vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear;
	vk::SamplerAddressMode addressModeU = vk::SamplerAddressMode::eRepeat;
	vk::SamplerAddressMode addressModeV = vk::SamplerAddressMode::eRepeat;
	vk::SamplerAddressMode addressModeW = vk::SamplerAddressMode::eRepeat;
};

/**
 * @brief RAII texture wrapping a VulkanImage and a matching vk::raii::Sampler.
 *
 * Supports loading from file (via STB), from raw pixel data, and creating
 * images suitable for framebuffer attachments. Mipmaps are generated
 * automatically for sampled textures.
 *
 * Non-copyable, movable.
 *
 * Usage:
 *   Texture tex = Texture::FromData(device, physDev, queue, qfi,
 *                                    512, 512, pixels, vk::Format::eR8G8B8A8Srgb);
 *   if (tex.IsValid()) { ... }
 */
class Texture
{
public:
	/** @brief Creates an empty, invalid Texture. Use factory methods to populate. */
	Texture() = default;

	~Texture() = default;

	// --- Non-copyable, movable ---
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	Texture(Texture&&) noexcept = default;
	Texture& operator=(Texture&&) noexcept = default;

	// --- Factory methods ---

	/**
	 * @brief Creates a Texture by loading an image file via stb_image.
	 *
	 * @param device           Logical device.
	 * @param physicalDevice   Physical device for memory queries.
	 * @param queue            Graphics queue for staging upload submits.
	 * @param queueFamilyIndex Queue family index for transient command pools.
	 * @param path             File path to the image (PNG, JPG, HDR, etc.).
	 * @param format           Desired Vulkan format (e.g., eR8G8B8A8Srgb for LDR,
	 *                         eR32G32B32A32Sfloat for HDR).
	 * @param config           Optional sampler configuration.
	 * @return A fully uploaded, mipmapped Texture, or an invalid Texture on failure.
	 */
	static Texture FromFile(const vk::raii::Device& device,
	                        const vk::raii::PhysicalDevice& physicalDevice,
	                        vk::Queue queue,
	                        uint32_t queueFamilyIndex,
	                        const char* path,
	                        vk::Format format,
	                        const SamplerConfig& config = {});

	/**
	 * @brief Creates a Texture from raw in-memory pixel data.
	 *
	 * Allocates a staging buffer, uploads the pixel data, copies it to a
	 * device-local VulkanImage, generates mipmaps, and transitions the layout
	 * to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
	 *
	 * @param device           Logical device.
	 * @param physicalDevice   Physical device for memory queries.
	 * @param queue            Graphics queue for staging upload submits.
	 * @param queueFamilyIndex Queue family index for transient command pools.
	 * @param width            Image width in pixels.
	 * @param height           Image height in pixels.
	 * @param pixelData        Pointer to raw pixel data. Must match the format's
	 *                         expected byte layout (e.g., 4 bytes/pixel for RGBA8).
	 * @param format           Vulkan format of the pixel data.
	 * @param config           Optional sampler configuration.
	 * @return A fully uploaded, mipmapped Texture, or an invalid Texture on failure.
	 */
	static Texture FromData(const vk::raii::Device& device,
	                        const vk::raii::PhysicalDevice& physicalDevice,
	                        vk::Queue queue,
	                        uint32_t queueFamilyIndex,
	                        uint32_t width,
	                        uint32_t height,
	                        const void* pixelData,
	                        vk::Format format,
	                        const SamplerConfig& config = {});

	/**
	 * @brief Creates a Texture suitable for framebuffer attachment usage.
	 *
	 * The image is created with the specified usage flags. No pixel data is
	 * uploaded and no mipmaps are generated (1 mip level). A sampler is
	 * created only if the usage includes VK_IMAGE_USAGE_SAMPLED_BIT.
	 *
	 * @param device         Logical device.
	 * @param physicalDevice Physical device for memory queries.
	 * @param extent         Attachment dimensions.
	 * @param format         Attachment format.
	 * @param usage          Image usage flags (e.g., eColorAttachment | eSampled).
	 * @param config         Optional sampler configuration.
	 * @return A valid Texture or an invalid Texture on failure.
	 */
	static Texture ForAttachment(const vk::raii::Device& device,
	                             const vk::raii::PhysicalDevice& physicalDevice,
	                             vk::Extent2D extent,
	                             vk::Format format,
	                             vk::ImageUsageFlags usage,
	                             const SamplerConfig& config = {});

	// --- Queries ---

	/** @brief True if the Texture contains a valid VulkanImage. */
	bool IsValid() const { return m_image != nullptr; }

	/** @brief Underlying VulkanImage (nullptr if invalid). */
	const VulkanImage* GetImage() const { return m_image.get(); }

	/** @brief Underlying VkSampler handle. Only valid if HasSampler() is true. */
	const vk::raii::Sampler& GetSampler() const { return m_sampler; }

	/** @brief True if a sampler was created for this Texture. */
	bool HasSampler() const { return *m_sampler != vk::Sampler{}; }

	/** @brief Image extent. Returns {0,0} if invalid. */
	vk::Extent2D Extent() const { return m_image ? m_image->Extent() : vk::Extent2D{}; }

	/** @brief Image format. Returns eUndefined if invalid. */
	vk::Format Format() const { return m_image ? m_image->Format() : vk::Format::eUndefined; }

	/** @brief Number of mip levels. Returns 0 if invalid. */
	uint32_t MipLevels() const { return m_image ? m_image->MipLevels() : 0; }

private:
	/**
	 * @brief Internal helper: creates the VulkanImage, uploads data, generates
	 *        mipmaps, creates the sampler, and transitions to SHADER_READ_ONLY.
	 */
	static Texture createFromPixelData(const vk::raii::Device& device,
	                                   const vk::raii::PhysicalDevice& physicalDevice,
	                                   vk::Queue queue,
	                                   uint32_t queueFamilyIndex,
	                                   uint32_t width,
	                                   uint32_t height,
	                                   const void* pixelData,
	                                   vk::DeviceSize dataSize,
	                                   vk::Format format,
	                                   bool generateMipmaps,
	                                   const SamplerConfig& config);

	/**
	 * @brief Computes the maximum number of mip levels for the given dimensions.
	 */
	static uint32_t computeMipLevels(uint32_t width, uint32_t height);

	std::unique_ptr<VulkanImage> m_image;
	vk::raii::Sampler m_sampler = nullptr;
};

} // namespace neurus
