#include "scene/Mesh.h"
#include "core/Log.h"
#include "asset/MeshData.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"
#include <cstring>

namespace neurus
{

Mesh::Mesh()
{
	o_type = ObjectID::GOType::GO_MESH;
	o_name = "Mesh";
}

Mesh::Mesh(const std::string& path)
	: Mesh()
{
	o_meshPath = path;
	if (path.empty()) return;
	auto meshData = std::make_shared<MeshData>();
	if (meshData->LoadObj(path)) o_mesh = meshData;
}

Mesh::~Mesh()
{
	ReleaseGPUBuffers();
}

void Mesh::ReloadMeshData(const std::string& assetDir)
{
	if (!o_meshPath.empty() && !o_mesh)
	{
		const std::string fullPath = assetDir.empty() ? o_meshPath : assetDir + "/" + o_meshPath;
		auto meshData = std::make_shared<MeshData>();
		if (meshData->LoadObj(fullPath)) o_mesh = meshData;
	}
}

void Mesh::UploadToGPU(const vk::raii::Device& device,
                       const vk::raii::PhysicalDevice& physicalDevice,
                       vk::Queue queue,
                       uint32_t queueFamilyIndex)
{
	if (!o_mesh) {
		NEURUS_ERR("[Mesh] UploadToGPU: mesh has no MeshData (ID=" << GetObjectID() << ")");
		return;
	}
	const auto& meshData = o_mesh->GetMeshData();
	if (meshData.dataArray.empty()) {
		NEURUS_ERR("[Mesh] UploadToGPU: mesh " << GetObjectID() << " has empty vertex data");
		return;
	}
	if (meshData.indexArray.empty()) {
		NEURUS_ERR("[Mesh] UploadToGPU: mesh " << GetObjectID() << " has empty index data");
		return;
	}

	constexpr size_t kSrcStride = 14;
	constexpr size_t kDstStride = 8;
	const size_t numVertices = meshData.dataArray.size() / kSrcStride;

	std::vector<float> strippedVertices(numVertices * kDstStride);
	for (size_t i = 0; i < numVertices; ++i) {
		std::memcpy(&strippedVertices[i * kDstStride],
		            &meshData.dataArray[i * kSrcStride],
		            kDstStride * sizeof(float));
	}

	const uint32_t vertexCount = static_cast<uint32_t>(numVertices);
	constexpr uint32_t kVertexStride = static_cast<uint32_t>(kDstStride * sizeof(float));
	const vk::DeviceSize vertexDataSize = static_cast<vk::DeviceSize>(strippedVertices.size() * sizeof(float));
	const uint32_t indexCount = static_cast<uint32_t>(meshData.indexArray.size());
	const vk::DeviceSize indexDataSize = static_cast<vk::DeviceSize>(indexCount * sizeof(uint32_t));

	m_gpuVertices = std::make_unique<VertexBuffer>(
		device, physicalDevice, queue, queueFamilyIndex,
		strippedVertices.data(), vertexDataSize, kVertexStride, vertexCount,
		("VBO_" + meshData.name).c_str());
	m_gpuIndices = std::make_unique<IndexBuffer>(
		device, physicalDevice, queue, queueFamilyIndex,
		meshData.indexArray.data(), indexDataSize, indexCount,
		("IBO_" + meshData.name).c_str());
	m_gpuIndexCount = indexCount;
	m_gpuDevice = &device;

	NEURUS_LOG("[Mesh] Uploaded mesh " << GetObjectID()
	           << " ('" << meshData.name << "')"
	           << " vertices=" << vertexCount
	           << " indices=" << indexCount);
}

void Mesh::ReleaseGPUBuffers()
{
	if (m_gpuDevice) {
		m_gpuDevice->waitIdle();
	}
	m_gpuVertices.reset();
	m_gpuIndices.reset();
	m_gpuIndexCount = 0;
}

void Mesh::SetObjShader(void* shader) { o_shader = shader; }

} // namespace neurus