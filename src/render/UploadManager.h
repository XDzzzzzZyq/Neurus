#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>

namespace neurus {

// Forward declarations
class IBLPass;
class Mesh;              // scene/Mesh.h
class Environment;       // scene/Environment.h
class Light;             // scene/Light.h
struct MeshGPU;          // render/resources/MeshGPU.h
struct EnvironmentGPU;   // render/resources/EnvironmentGPU.h
struct LightGPU;         // render/resources/LightGPU.h

/**
 * @brief CPU-to-GPU upload service.
 *
 * Owns a separate command pool and IBLPass for asynchronous uploads.
 * Stateless — each Upload*() method performs one-shot construction and
 * returns the GPU resource by value.
 */
class UploadManager
{
public:
	UploadManager(const vk::raii::Device& device,
	              const vk::raii::PhysicalDevice& physicalDevice,
	              uint32_t queueFamilyIndex);
	~UploadManager();

	// Non-copyable
	UploadManager(const UploadManager&) = delete;
	UploadManager& operator=(const UploadManager&) = delete;

	// Non-movable (owns IBLPass internally via unique_ptr)
	UploadManager(UploadManager&&) = delete;
	UploadManager& operator=(UploadManager&&) = delete;

	/** @brief Upload mesh geometry to GPU. Returns device-local buffers by value. */
	MeshGPU UploadMesh(const Mesh& mesh);

	/** @brief Upload environment map to GPU. Generates diffuse + specular cubemaps. */
	EnvironmentGPU UploadEnvironment(const Environment& env);

	/** @brief Upload light shadow resources to GPU. Creates shadow depth map. */
	LightGPU UploadLight(const Light& light);

	/** @brief Wait for all pending upload operations to complete. */
	void WaitIdle();

private:
	const vk::raii::Device* um_device = nullptr;
	const vk::raii::PhysicalDevice* um_physicalDevice = nullptr;
	vk::raii::CommandPool um_commandPool{ nullptr };
	vk::Queue um_queue = nullptr;
	uint32_t um_queueFamilyIndex = 0;

	/** @brief Internal IBL pass for environment map generation on upload. */
	std::unique_ptr<class IBLPass> um_iblPass;
};

} // namespace neurus
