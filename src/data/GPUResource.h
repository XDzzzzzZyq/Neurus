#pragma once

#include <cstdint>

namespace neurus {

/**
 * @brief Abstract base class for all GPU-owned resources.
 *
 * Enforces RAII contract for Vulkan resource wrappers:
 * resources are allocated in derived constructors and freed in destructors.
 * Copy semantics are deleted to prevent dangling GPU handles.
 */
class GPUResource
{
public:
	GPUResource() = default;
	virtual ~GPUResource() = default;

	// GPU resources must not be copied (would double-free on destruction)
	GPUResource(const GPUResource&) = delete;
	GPUResource& operator=(const GPUResource&) = delete;

	// Move semantics are allowed (transfer ownership of GPU handles)
	GPUResource(GPUResource&&) noexcept = default;
	GPUResource& operator=(GPUResource&&) noexcept = default;

	/**
	 * @brief Returns true if the underlying GPU resource was successfully created.
	 * @note Derived classes must override this to check their specific handles.
	 */
	virtual bool IsValid() const = 0;
};

} // namespace neurus
