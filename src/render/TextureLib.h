#pragma once

#include "Texture.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace neurus {

/**
 * @brief Global texture cache providing load-once semantics.
 *
 * LoadTexture() returns a cached shared_ptr<Texture> if the path has
 * already been loaded; otherwise it loads the file, inserts it into the
 * cache, and returns a new entry.
 *
 * Thread safety: main-thread only. No internal locking.
 *
 * Usage:
 *   auto tex = TextureLib::LoadTexture(device, physDev, queue, qfi,
 *                                       "textures/brick.png",
 *                                       vk::Format::eR8G8B8A8Srgb);
 *   if (tex->IsValid()) { ... }
 */
class TextureLib
{
public:
	/**
	 * @brief Loads (or retrieves from cache) a texture from a file path.
	 *
	 * @param device           Logical device.
	 * @param physicalDevice   Physical device for memory queries.
	 * @param queue            Graphics queue for staging upload.
	 * @param queueFamilyIndex Queue family index for transient command pools.
	 * @param path             File path to the image.
	 * @param format           Desired Vulkan format.
	 * @param config           Optional sampler configuration.
	 * @return Shared pointer to the cached (or newly loaded) Texture.
	 *         May be invalid if loading fails.
	 */
	static std::shared_ptr<Texture> LoadTexture(const vk::raii::Device& device,
	                                            const vk::raii::PhysicalDevice& physicalDevice,
	                                            vk::Queue queue,
	                                            uint32_t queueFamilyIndex,
	                                            const char* path,
	                                            vk::Format format,
	                                            const SamplerConfig& config = {});

	/**
	 * @brief Removes a texture from the cache by path.
	 * @param path File path used as the cache key.
	 */
	static void UnloadTexture(const std::string& path);

	/**
	 * @brief Clears all cached textures.
	 */
	static void Clear();

	/**
	 * @brief Returns the number of cached textures.
	 */
	static size_t CacheSize();

private:
	// Hidden constructor — static-only class
	TextureLib() = default;

	static std::unordered_map<std::string, std::shared_ptr<Texture>> s_cache;
};

} // namespace neurus
