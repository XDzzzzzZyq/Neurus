#pragma once

#include "ShaderModule.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace neurus {

/**
 * @brief Thread-local shader module cache keyed by shader name.
 *
 * LoadShader() creates a ShaderModule from embedded SPIR-V and caches it.
 * Subsequent calls with the same name return the cached shared_ptr.
 *
 * @note NOT thread-safe (main thread only for MVP).
 */
class ShaderLib
{
public:
	/**
	 * @brief Loads (or retrieves cached) a shader from embedded SPIR-V data.
	 * @param device Logical device.
	 * @param name Unique shader name used as the cache key.
	 * @param data Pointer to embedded uint32_t SPIR-V data.
	 * @param size Size of the data array in bytes.
	 * @return Shared pointer to the cached or newly-created ShaderModule.
	 */
	static std::shared_ptr<ShaderModule> LoadShader(
		const vk::raii::Device& device,
		const std::string& name,
		const uint32_t* data,
		size_t size);

	/** @brief Removes all cached shader modules from the library. */
	static void Clear();

private:
	/** @brief Cache mapping shader names to ShaderModule shared pointers. */
	static inline std::unordered_map<std::string, std::shared_ptr<ShaderModule>> s_cache;
};

} // namespace neurus
