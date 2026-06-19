/**
 * @file TestCornellBox.h
 * @brief Header-only helper for loading the Cornell Box scene in GPU tests.
 *
 * Provides LoadCornellBox() which loads 6 OBJ meshes (back_wall, left_wall,
 * right_wall, left_box, right_box, light_up), uploads them to GPU vertex/index
 * buffers, and returns render items + camera + light for the scene.
 *
 * Usage:
 * @code
 *   auto cb = LoadCornellBox(device, pd, queue, qfi);
 *   m_geometryPass->Record(cmd, camUBO, cb.renderItems, extent);
 * @endcode
 *
 * The Cornell Box occupies approximately [-1, 1] in all axes, making it
 * suitable for SSAO testing (corners + crevices produce visible occlusion).
 */

#pragma once

#include "Log.h"
#include "render/passes/GeometryPass.h"
#include "render/Material.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include "asset/MeshData.h"

#include "scene/Camera.h"
#include "scene/Light.h"

#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>
#include <vector>

namespace neurus {
namespace test {

// ---------------------------------------------------------------------------
// Test vertex structure (matches BufferLayout: pos(3) + normal(3) + uv(2))
// ---------------------------------------------------------------------------
struct CornellBoxVertex
{
	float posX, posY, posZ;
	float nrmX, nrmY, nrmZ;
	float uvX,  uvY;
};

/**
 * @brief Per-mesh entry: owns the GPU buffers and exposes the render item.
 */
struct CornellMeshEntry
{
	std::unique_ptr<VertexBuffer> vertexBuffer;
	std::unique_ptr<IndexBuffer>  indexBuffer;
	GeometryRenderItem            renderItem = {};
};

/**
 * @brief Aggregated result of loading the Cornell Box scene.
 *
 * All GPU buffers are owned by this struct (via meshes vector).
 * renderItems provides a flat list for direct use with GeometryPass::Record.
 */
struct CornellBoxResources
{
	std::vector<CornellMeshEntry> meshes;
	std::vector<GeometryRenderItem> renderItems;

	std::shared_ptr<Camera> camera;
	std::shared_ptr<Light>  light;
};

/**
 * @brief Loads the Cornell Box scene from OBJ files and uploads to GPU.
 *
 * Loads 6 OBJ meshes with their respective material colours:
 *   back_wall    — white (1.0, 1.0, 1.0)
 *   left_wall    — red   (1.0, 0.0, 0.0)
 *   right_wall   — green (0.0, 1.0, 0.0)
 *   left_box     — white (1.0, 1.0, 1.0)
 *   right_box    — white (1.0, 1.0, 1.0)
 *   light_up     — emissive white (used as geometry)
 *
 * Creates a default camera at (0, 1, 3) looking at origin and a single
 * point light at (-0.3, 0.8, 1.0) with warm white colour.
 *
 * @note All buffers use device-local memory. A staging upload is performed
 *       synchronously on the provided graphics queue.
 * @note The OBJ files must be relative to ResolveAssetPath("res/obj/cornellbox/").
 *
 * @param device           Logical device.
 * @param physicalDevice   Physical device (for memory properties).
 * @param graphicsQueue    Graphics queue for staging uploads.
 * @param queueFamilyIndex Queue family index for VulkanBuffer creation.
 * @param basePath         Base path for OBJ files (default: "res/obj/cornellbox/").
 * @return Fully populated CornellBoxResources (move-only due to unique_ptr members).
 */
inline CornellBoxResources LoadCornellBox(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue graphicsQueue,
	uint32_t queueFamilyIndex,
	const std::string& basePath = "res/obj/cornellbox/")
{
	CornellBoxResources res;

	// --- OBJ file names and their material colours ---
	struct ObjEntry
	{
		const char* filename;
		glm::vec3   albedo;
		float       metallic;
		float       roughness;
	};

	const std::vector<ObjEntry> entries = {
		{"back_wall.obj",  glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.5f},
		{"left_wall.obj",  glm::vec3(1.0f, 0.0f, 0.0f), 0.0f, 0.5f},
		{"right_wall.obj", glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.5f},
		{"left_box.obj",   glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.5f},
		{"right_box.obj",  glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.5f},
		{"light_up.obj",   glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, 0.5f},
	};

	res.meshes.reserve(entries.size());
	res.renderItems.reserve(entries.size());

	for (const auto& entry : entries)
	{
		// --- Load OBJ (try multiple relative paths) ---
		const std::string relPath = basePath + entry.filename;
		std::string objPath = relPath;
		{
			// CTest runs from build/debug/test/ or build/debug/
			std::ifstream f(std::string("../../../") + relPath);
			if (f.good()) { objPath = std::string("../../../") + relPath; }
			else { f = std::ifstream(std::string("../../") + relPath); if (f.good()) objPath = std::string("../../") + relPath; }
		}

		auto meshData = std::make_shared<MeshData>();
		if (!meshData->LoadObj(objPath))
		{
			NEURUS_ERR("LoadCornellBox: Failed to load " << objPath);
			continue;
		}

		const auto& rawMesh = meshData->GetMeshData();
		const size_t srcVertexCount = rawMesh.dataArray.size() / 14;
		const size_t indexCount = rawMesh.indexArray.size();

		if (srcVertexCount == 0 || indexCount == 0)
		{
			NEURUS_ERR("LoadCornellBox: Empty geometry in " << entry.filename);
			continue;
		}

		// --- Convert vertex data (14 floats → 8: pos+nrm+uv) ---
		std::vector<CornellBoxVertex> vertices(srcVertexCount);
		for (size_t i = 0; i < srcVertexCount; ++i)
		{
			const float* s = &rawMesh.dataArray[i * 14];
			vertices[i].posX = s[0];
			vertices[i].posY = s[1];
			vertices[i].posZ = s[2];
			vertices[i].nrmX = s[3];
			vertices[i].nrmY = s[4];
			vertices[i].nrmZ = s[5];
			vertices[i].uvX  = s[6];
			vertices[i].uvY  = s[7];
		}

		std::vector<uint32_t> indices = rawMesh.indexArray;

		// --- Upload to GPU ---
		CornellMeshEntry meshEntry;

		meshEntry.vertexBuffer = std::make_unique<VertexBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			vertices.data(), vertices.size() * sizeof(CornellBoxVertex),
			sizeof(CornellBoxVertex), static_cast<uint32_t>(vertices.size()));

		meshEntry.indexBuffer = std::make_unique<IndexBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			indices.data(), indices.size() * sizeof(uint32_t),
			static_cast<uint32_t>(indices.size()));

		// --- Build render item ---
		GeometryRenderItem& item = meshEntry.renderItem;
		item.vertexBuffer = meshEntry.vertexBuffer->buffer();
		item.indexBuffer  = meshEntry.indexBuffer->buffer();
		item.indexCount   = meshEntry.indexBuffer->GetIndexCount();
		item.indexType    = meshEntry.indexBuffer->GetIndexType();
		// Identity model matrix (OBJ coordinates are already world-space)
		item.pushConstants.model = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		res.meshes.push_back(std::move(meshEntry));
		res.renderItems.push_back(res.meshes.back().renderItem);
	}

	// --- Create default camera ---
	// Positioned to view the interior of the box from slightly above.
	res.camera = std::make_shared<Camera>(
		static_cast<float>(256),  // width (placeholder, caller adjusts)
		static_cast<float>(256),  // height
		60.0f, 0.1f, 100.0f);
	res.camera->SetCamPos(glm::vec3(-2.5f, 0.0f, 1.0f));
	res.camera->SetTarPos(glm::vec3(0.0f, 0.0f, 1.0f));
	res.camera->SetRotation(glm::vec3(0.0f, 0.0f, -90.0f));

	// --- Create a point light near the ceiling ---
	res.light = std::make_shared<Light>(LightType::POINTLIGHT, 30.0f,
	                                    glm::vec3(1.0f, 0.95f, 0.8f));
	res.light->SetPosition(glm::vec3(-0.3f, 0.8f, 1.0f));

	NEURUS_LOG("[LoadCornellBox] Loaded " << res.renderItems.size()
	           << " meshes from " << basePath);

	return res;
}

} // namespace test
} // namespace neurus
