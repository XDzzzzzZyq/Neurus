/**
 * @file TestDeferredScene.h
 * @brief Header-only helper for loading the deferred shading test scene in GPU tests.
 *
 * Provides BuildDeferredScene() which loads a sphere OBJ mesh, uploads it to GPU
 * via Mesh::UploadToGPU(), creates a default camera, point light, and PBR material,
 * and returns all resources in a DeferredSceneResources struct.
 *
 * Usage:
 * @code
 *   auto path = ResolveAssetPath("res/obj/sphere.obj");
 *   auto scene = BuildDeferredScene(device, pd, queue, qfi, path);
 *   std::vector<GeometryRenderItem> items = { scene.renderItem };
 *   m_geometryPass->Record(cmd, cache, ctx);  // ctx.renderItems = items
 * @endcode
 *
 * Scene layout:
 *   - Sphere at origin, scaled by 0.25
 *   - Camera at (0, 2, 5) looking at origin, 60° FOV
 *   - Point light at (2, 2, 2), power 10, radius 10, white
 *   - Material: metallic 0, roughness 0.5, albedo white
 */

#pragma once

#include "Log.h"
#include "render/passes/GeometryPass.h"
#include "render/Material.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include "asset/MeshData.h"

#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"

#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>

namespace neurus {
namespace test {

/**
 * @brief Aggregated result of building the deferred shading test scene.
 *
 * Owns all scene resources via shared_ptr. The renderItem holds raw
 * vk::Buffer handles into the mesh's GPU buffers, which remain valid
 * as long as the mesh shared_ptr is alive.
 */
struct DeferredSceneResources
{
	std::shared_ptr<Mesh>     mesh;
	std::shared_ptr<Camera>   camera;
	std::shared_ptr<Light>    light;
	std::shared_ptr<Material> material;
	GeometryRenderItem        renderItem = {};
};

/**
 * @brief Builds a deferred shading test scene: sphere mesh + camera + light + material.
 *
 * Loads the OBJ file at @p meshPath, creates a PBR material (metallic=0,
 * roughness=0.5, albedo=white), uploads the mesh to GPU, and assembles a
 * GeometryRenderItem with 0.25x scale.
 *
 * Camera: position (0, 2, 5), target (0, 0, 0), 60° FOV.
 * Light:  POINTLIGHT at (2, 2, 2), power 10, radius 10, colour white.
 *
 * @param device           Logical device.
 * @param physicalDevice   Physical device (for memory properties).
 * @param graphicsQueue    Graphics queue for staging uploads.
 * @param queueFamilyIndex Queue family index for temporary command pool.
 * @param meshPath         Path to the sphere OBJ file (caller resolves).
 * @param width            Viewport width for camera aspect ratio (default 256).
 * @param height           Viewport height for camera aspect ratio (default 256).
 * @return Fully populated DeferredSceneResources.
 */
inline DeferredSceneResources BuildDeferredScene(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue graphicsQueue,
	uint32_t queueFamilyIndex,
	const std::string& meshPath,
	float width = 256.0f,
	float height = 256.0f)
{
	DeferredSceneResources res;

	// --- Load sphere OBJ ---
	auto meshData = std::make_shared<MeshData>();
	if (!meshData->LoadObj(meshPath))
	{
		NEURUS_ERR("BuildDeferredScene: Failed to load " << meshPath);
		return res;
	}

	const auto& rawMesh = meshData->GetMeshData();
	if (rawMesh.dataArray.empty() || rawMesh.indexArray.empty())
	{
		NEURUS_ERR("BuildDeferredScene: Empty geometry in " << meshPath);
		return res;
	}

	// --- Create material (metallic=0, roughness=0.5, albedo=white) ---
	res.material = std::make_shared<Material>();
	res.material->SetMatParam(Material::MAT_METAL, 0.0f);
	res.material->SetMatParam(Material::MAT_ROUGH, 0.5f);
	res.material->SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f));

	// --- Create mesh and upload to GPU ---
	res.mesh = std::make_shared<Mesh>();
	res.mesh->o_mesh = meshData;
	res.mesh->o_material = res.material;
	res.mesh->UploadToGPU(device, physicalDevice, graphicsQueue, queueFamilyIndex);

	// --- Build render item (scale 0.25x) ---
	GeometryRenderItem& item = res.renderItem;
	item.vertexBuffer = res.mesh->GetVertexBuffer()->buffer();
	item.indexBuffer  = res.mesh->GetIndexBuffer()->buffer();
	item.indexCount   = res.mesh->GetGPUIndexCount();
	item.indexType    = res.mesh->GetIndexBuffer()->GetIndexType();
	item.pushConstants.model = glm::scale(glm::mat4(1.0f), glm::vec3(0.25f));
	item.pushConstants.normalMatrix = glm::mat4(1.0f);

	// --- Create camera (pos 0,2,5, target origin, 60° FOV) ---
	res.camera = std::make_shared<Camera>(width, height, 60.0f, 0.1f, 100.0f);
	res.camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	res.camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	// --- Create point light (pos 2,2,2, power 10, radius 10, white) ---
	res.light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
	res.light->SetPosition(glm::vec3(2.0f, 2.0f, 2.0f));
	res.light->light_radius = 10.0f;

	NEURUS_LOG("[BuildDeferredScene] Loaded sphere scene from " << meshPath);

	return res;
}

} // namespace test
} // namespace neurus
