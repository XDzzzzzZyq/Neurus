/**
 * @file GPUResourceCache.cpp
 * @brief GPU resource cache implementation — mesh buffers and light SSBO.
 */

#include "GPUResourceCache.h"

#include "core/Log.h"

#include "data/MeshData.h"
#include "render/LightingPass.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <cstring>
#include <vector>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GPUResourceCache::GPUResourceCache(const vk::raii::Device& device,
                                   const vk::raii::PhysicalDevice& physicalDevice,
                                   vk::Queue graphicsQueue,
                                   uint32_t queueFamilyIndex)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
	, m_graphicsQueue(graphicsQueue)
	, m_queueFamilyIndex(queueFamilyIndex)
	, m_lightCount(0)
{
	NEURUS_LOG("[GPUResourceCache] Created (queueFamily=" << queueFamilyIndex << ")");
}

GPUResourceCache::~GPUResourceCache()
{
	Clear();
}

// ---------------------------------------------------------------------------
// Mesh buffer management
// ---------------------------------------------------------------------------

void GPUResourceCache::UploadMesh(const Mesh& mesh)
{
	if (!mesh.o_mesh)
	{
		NEURUS_ERR("[GPUResourceCache] UploadMesh: mesh has no MeshData (ID=" << mesh.GetObjectID() << ")");
		return;
	}

	const int meshID = mesh.GetObjectID();
	const auto& meshData = mesh.o_mesh->GetMeshData();

	if (meshData.dataArray.empty())
	{
		NEURUS_ERR("[GPUResourceCache] UploadMesh: mesh " << meshID << " has empty vertex data");
		return;
	}

	if (meshData.indexArray.empty())
	{
		NEURUS_ERR("[GPUResourceCache] UploadMesh: mesh " << meshID << " has empty index data");
		return;
	}

	// Remove existing buffers for this mesh ID before re-uploading
	RemoveMesh(meshID);

	// --- Strip 14-float vertices → 8-float (pos+normal+uv) ---
	// MeshData stores 14 floats per vertex:
	//   pos(3) + normal(3) + uv(2) + tangent(3) + bitangent(3)
	// GeometryPass needs 8 floats: pos(3) + normal(3) + uv(2)
	constexpr size_t kSrcStride = 14;
	constexpr size_t kDstStride = 8;
	const size_t numVertices = meshData.dataArray.size() / kSrcStride;

	std::vector<float> strippedVertices(numVertices * kDstStride);
	for (size_t i = 0; i < numVertices; ++i)
	{
		std::memcpy(&strippedVertices[i * kDstStride],
		            &meshData.dataArray[i * kSrcStride],
		            kDstStride * sizeof(float));
	}

	const uint32_t vertexCount = static_cast<uint32_t>(numVertices);
	constexpr uint32_t kVertexStride = static_cast<uint32_t>(kDstStride * sizeof(float));  // 32 bytes
	const vk::DeviceSize vertexDataSize = static_cast<vk::DeviceSize>(strippedVertices.size() * sizeof(float));

	const uint32_t indexCount = static_cast<uint32_t>(meshData.indexArray.size());
	const vk::DeviceSize indexDataSize = static_cast<vk::DeviceSize>(indexCount * sizeof(uint32_t));

	// --- Create GPU buffers ---
	auto& entry = m_meshBuffers[meshID];
	entry.vertexBuffer = std::make_unique<VertexBuffer>(
		*m_device, *m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex,
		strippedVertices.data(), vertexDataSize, kVertexStride, vertexCount);
	entry.indexBuffer = std::make_unique<IndexBuffer>(
		*m_device, *m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex,
		meshData.indexArray.data(), indexDataSize, indexCount);
	entry.indexCount = indexCount;

	NEURUS_LOG("[GPUResourceCache] Uploaded mesh " << meshID
	           << " ('" << meshData.name << "')"
	           << " vertices=" << vertexCount
	           << " indices=" << indexCount
	           << " totalBuffers=" << m_meshBuffers.size());
}

const VertexBuffer* GPUResourceCache::GetVertexBuffer(int meshID) const
{
	const auto it = m_meshBuffers.find(meshID);
	if (it == m_meshBuffers.end())
	{
		return nullptr;
	}
	return it->second.vertexBuffer.get();
}

const IndexBuffer* GPUResourceCache::GetIndexBuffer(int meshID) const
{
	const auto it = m_meshBuffers.find(meshID);
	if (it == m_meshBuffers.end())
	{
		return nullptr;
	}
	return it->second.indexBuffer.get();
}

uint32_t GPUResourceCache::GetIndexCount(int meshID) const
{
	const auto it = m_meshBuffers.find(meshID);
	if (it == m_meshBuffers.end())
	{
		return 0;
	}
	return it->second.indexCount;
}

void GPUResourceCache::RemoveMesh(int meshID)
{
	const auto it = m_meshBuffers.find(meshID);
	if (it == m_meshBuffers.end())
	{
		return;  // Not cached — no-op
	}

	// unique_ptr destructors release GPU resources
	m_meshBuffers.erase(it);
	NEURUS_LOG("[GPUResourceCache] Removed mesh " << meshID
	           << " (remaining=" << m_meshBuffers.size() << ")");
}

// ---------------------------------------------------------------------------
// Light buffer management
// ---------------------------------------------------------------------------

void GPUResourceCache::UploadLights(const Scene& scene)
{
	// Collect only point lights — LightingPass currently handles POINTLIGHT only
	std::vector<PointLightGpu> gpuLights;
	gpuLights.reserve(scene.light_list.size());

	for (const auto& [id, light] : scene.light_list)
	{
		if (light->light_type != LightType::POINTLIGHT)
		{
			continue;
		}

		PointLightGpu gpu = {};
		const auto& pos = light->GetPosition();

		// Position (world-space, from Transform3D)
		gpu.posX = pos.x;
		gpu.posY = pos.y;
		gpu.posZ = pos.z;

		// Color (linear RGB)
		gpu.colorR = light->light_color.r;
		gpu.colorG = light->light_color.g;
		gpu.colorB = light->light_color.b;

		// Lighting parameters
		gpu.power = light->light_power;
		gpu.radius = light->light_radius;

		gpuLights.push_back(gpu);
	}

	const uint32_t newCount = static_cast<uint32_t>(gpuLights.size());
	m_lightCount = newCount;

	if (newCount == 0)
	{
		m_lightSSBO.reset();
		NEURUS_LOG("[GPUResourceCache] No point lights in scene — SSBO released");
		return;
	}

	// Create or re-create the SSBO
	const vk::DeviceSize bufferSize = newCount * sizeof(PointLightGpu);

	m_lightSSBO = std::make_unique<VulkanBuffer>(
		*m_device, *m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex,
		bufferSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal);
	m_lightSSBO->Upload(gpuLights.data(), bufferSize);

	NEURUS_LOG("[GPUResourceCache] Uploaded " << newCount << " point lights"
	           << " (" << bufferSize << " bytes)");
}

const VulkanBuffer* GPUResourceCache::GetLightSSBO() const
{
	return m_lightSSBO.get();
}

uint32_t GPUResourceCache::GetLightCount() const
{
	return m_lightCount;
}

// ---------------------------------------------------------------------------
// Bulk operations
// ---------------------------------------------------------------------------

void GPUResourceCache::Clear()
{
	// unique_ptr destructors release GPU resources automatically
	m_meshBuffers.clear();
	m_lightSSBO.reset();
	m_lightCount = 0;

	NEURUS_LOG("[GPUResourceCache] Cleared all cached GPU resources");
}

} // namespace neurus
