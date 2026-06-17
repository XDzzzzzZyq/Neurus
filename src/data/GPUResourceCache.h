/**
 * @file GPUResourceCache.h
 * @brief Centralized GPU resource ownership for mesh and light buffers.
 *
 * GPUResourceCache owns all GPU buffers (vertex, index, light SSBO) outside
 * the renderer. The DeferredRenderer holds non-owning pointers into the cache,
 * enabling the Data & Resource layer to manage GPU resource lifetimes
 * independently of the rendering pipeline.
 *
 * Architecture:
 * - Lives in Data & Resource layer (src/data/)
 * - Owns VertexBuffer, IndexBuffer (mesh geometry) and VulkanBuffer (light SSBO)
 * - Renderer queries cache for buffer pointers each frame
 * - Non-copyable, RAII — destructor releases all GPU resources
 *
 * @note Cache does not handle descriptor sets or pipelines — that is the
 *       renderer's responsibility.
 */

#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace neurus {

// --- Forward declarations ---
class VertexBuffer;
class IndexBuffer;
class VulkanBuffer;
class Mesh;
class Scene;

/**
 * @brief GPU buffer cache managing vertex/index buffers and light SSBOs.
 *
 * Takes non-owning references to the Vulkan device and queue on construction.
 * Mesh buffers are uploaded on-demand via UploadMesh(), keyed by mesh UID.
 * Light SSBO is rebuilt from the scene's light list on UploadLights().
 *
 * All getter methods return nullptr if the requested resource is not present
 * in the cache (e.g., mesh not uploaded, no lights in scene).
 *
 * Usage:
 *   GPUResourceCache cache(device, physDevice, graphicsQueue, queueFamilyIndex);
 *   cache.UploadMesh(aMesh);
 *   cache.UploadLights(scene);
 *   // Renderer queries each frame:
 *   const auto* vbo = cache.GetVertexBuffer(meshId);
 *   const auto* ibo = cache.GetIndexBuffer(meshId);
 *   const auto* ssbo = cache.GetLightSSBO();
 */
class GPUResourceCache
{
public:
	/**
	 * @brief Constructs the cache with borrowed Vulkan device handles.
	 *
	 * @param device           Logical device (borrowed, must outlive cache).
	 * @param physicalDevice   Physical device for memory queries (borrowed).
	 * @param graphicsQueue    Graphics queue for staging upload submits.
	 * @param queueFamilyIndex Queue family index for temp command pool creation.
	 */
	GPUResourceCache(const vk::raii::Device& device,
	                 const vk::raii::PhysicalDevice& physicalDevice,
	                 vk::Queue graphicsQueue,
	                 uint32_t queueFamilyIndex);

	~GPUResourceCache();

	// --- Non-copyable (owns GPU resources) ---
	GPUResourceCache(const GPUResourceCache&) = delete;
	GPUResourceCache& operator=(const GPUResourceCache&) = delete;
	GPUResourceCache(GPUResourceCache&&) = delete;
	GPUResourceCache& operator=(GPUResourceCache&&) = delete;

	// -------------------------------------------------------------------
	// Mesh buffer management
	// -------------------------------------------------------------------

	/**
	 * @brief Uploads vertex and index data for a mesh to GPU buffers.
	 *
	 * Reads MeshData from the Mesh, strips 14-float vertices to the
	 * 8-float GeometryPass format (pos+normal+uv), and creates
	 * device-local VertexBuffer + IndexBuffer via staging upload.
	 *
	 * If a mesh with the same UID was already uploaded, its existing
	 * buffers are first released (re-upload).
	 *
	 * @param mesh Mesh with valid o_mesh (MeshData) containing vertex/index arrays.
	 * @note No-op if mesh.o_mesh is nullptr or vertex/index arrays are empty.
	 */
	void UploadMesh(const Mesh& mesh);

	/**
	 * @brief Returns the vertex buffer for a previously uploaded mesh.
	 * @param meshID Mesh UID (from Mesh::GetObjectID()).
	 * @return Non-owning pointer to VertexBuffer, or nullptr if not found.
	 */
	const VertexBuffer* GetVertexBuffer(int meshID) const;

	/**
	 * @brief Returns the index buffer for a previously uploaded mesh.
	 * @param meshID Mesh UID (from Mesh::GetObjectID()).
	 * @return Non-owning pointer to IndexBuffer, or nullptr if not found.
	 */
	const IndexBuffer* GetIndexBuffer(int meshID) const;

	/**
	 * @brief Returns the number of indices for a previously uploaded mesh.
	 * @param meshID Mesh UID (from Mesh::GetObjectID()).
	 * @return Index count, or 0 if not found.
	 */
	uint32_t GetIndexCount(int meshID) const;

	/**
	 * @brief Releases GPU buffers for the specified mesh.
	 * @param meshID Mesh UID to remove from cache.
	 * @note Safe to call multiple times; no-op if mesh is not cached.
	 */
	void RemoveMesh(int meshID);

	// -------------------------------------------------------------------
	// Light buffer management
	// -------------------------------------------------------------------

	/**
	 * @brief Converts scene point lights to PointLightGpu and uploads as SSBO.
	 *
	 * Iterates scene.light_list, filters to POINTLIGHT type, converts
	 * each Light to a PointLightGpu struct (std140, 48 bytes), and
	 * uploads the array as a device-local storage buffer.
	 *
	 * If the scene has no point lights, the SSBO is released and
	 * GetLightSSBO() returns nullptr.
	 *
	 * @param scene Scene containing the light list.
	 */
	void UploadLights(const Scene& scene);

	/**
	 * @brief Returns the light SSBO, or nullptr if no lights are cached.
	 * @return Non-owning pointer to VulkanBuffer, or nullptr.
	 */
	const VulkanBuffer* GetLightSSBO() const;

	/**
	 * @brief Returns the number of point lights in the cached SSBO.
	 * @return Light count (0 if no lights or SSBO not allocated).
	 */
	uint32_t GetLightCount() const;

	// -------------------------------------------------------------------
	// Bulk operations
	// -------------------------------------------------------------------

	/**
	 * @brief Releases all GPU buffers held by the cache.
	 *
	 * After Clear(), all getters return nullptr.
	 * Safe to call before destruction or to reset cache state.
	 */
	void Clear();

private:
	// --- Non-owning Vulkan handles ---
	const vk::raii::Device* m_device;
	const vk::raii::PhysicalDevice* m_physicalDevice;
	vk::Queue m_graphicsQueue;
	uint32_t m_queueFamilyIndex;

	// --- Owned mesh GPU resources ---
	struct MeshGPUBuffers
	{
		std::unique_ptr<VertexBuffer> vertexBuffer;
		std::unique_ptr<IndexBuffer> indexBuffer;
		uint32_t indexCount = 0;
	};
	std::unordered_map<int, MeshGPUBuffers> m_meshBuffers;

	// --- Owned light SSBO ---
	std::unique_ptr<VulkanBuffer> m_lightSSBO;
	uint32_t m_lightCount = 0;
};

} // namespace neurus
