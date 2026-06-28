/**
 * @file TestMultiShadow.h
 * @brief Header-only parametrized cube-on-plane scene builder with N point lights.
 *
 * Provides LoadMultiShadow() which procedurally generates a unit cube
 * (centered at origin, positioned at (0,3,0) via SetPosition) and a large
 * ground plane (y=0, [-10,10] in XZ), with N shadow-casting point lights.
 *
 * Usage:
 * @code
 *   auto res = LoadMultiShadow(device, pd, queue, qfi);        // 2 lights (default)
 *   auto res = LoadMultiShadow(device, pd, queue, qfi, 4);     // 4 lights
 * @endcode
 *
 * Geometry is 100% procedural via OBJ strings -- no OBJ files needed.
 */

#pragma once

#include "Log.h"
#include "scene/Scene.h"
#include "scene/Mesh.h"
#include "scene/Light.h"
#include "scene/Camera.h"
#include "asset/MeshData.h"
#include "render/buffers/VertexBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/passes/GeometryPass.h"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace neurus {
namespace test {

/**
 * @brief Aggregated result of building the multi-shadow test scene.
 *
 * The Scene owns all GPU resources (meshes hold VertexBuffer/IndexBuffer
 * internally).  renderItems provides a flat list for direct use with
 * ShadowDepthPass.  lightUIDs provides the unique IDs of all created lights
 * for per-light shadow map lookup.
 */
struct MultiShadowResources
{
	std::shared_ptr<Scene> scene;
	std::vector<GeometryRenderItem> renderItems;
	std::vector<int> lightUIDs;
};

/**
 * @brief Builds the multi-shadow test scene procedurally with N point lights.
 *
 * Generates:
 *   - A unit cube (12 triangles, 8 vertices, 36 indices) centred at origin
 *     covering [-0.5, +0.5]^3, positioned at (0,3,0) via SetPosition.
 *   - A ground-plane quad (2 triangles, 4 vertices, 6 indices) at y=0,
 *     spanning [-10,10] in XZ, facing +Y.
 *
 * The plane uses identity; the cube's model matrix shifts it to (0, 3, 0).
 * Lights are placed at y=6.  With the default count of 2, the two lights
 * are at (0, 6, 0) and (-6, 6, 0).  For other counts the lights are
 * distributed evenly in a circle of radius 6 around the cube.
 *
 * All lights have power=10, color=white, and shadow=true.
 *
 * @note All buffers use device-local memory.  A staging upload is performed
 *       synchronously on the provided graphics queue via Mesh::UploadToGPU().
 *
 * @param device           Logical device.
 * @param physicalDevice   Physical device (for memory properties).
 * @param graphicsQueue    Graphics queue for staging uploads.
 * @param queueFamilyIndex Queue family index for GPUBuffer creation.
 * @param numLights        Number of shadow-casting point lights (default: 2).
 * @return Fully populated MultiShadowResources with scene, renderItems,
 *         and lightUIDs.
 */
inline MultiShadowResources LoadMultiShadow(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue graphicsQueue,
	uint32_t queueFamilyIndex,
	int numLights = 2)
{
	MultiShadowResources res;
	res.scene = std::make_shared<Scene>();

	// ===================================================================
	//  1. Cube: unit cube centred at origin [-0.5, +0.5]^3
	//     8 unique vertices, 12 triangles (36 indices)
	// ===================================================================

	const char* kCubeObj = R"OBJ(
v -0.5 -0.5 -0.5
v 0.5 -0.5 -0.5
v 0.5 -0.5 0.5
v -0.5 -0.5 0.5
v -0.5 0.5 -0.5
v 0.5 0.5 -0.5
v 0.5 0.5 0.5
v -0.5 0.5 0.5

f 1 2 3 4
f 5 8 7 6
f 1 5 6 2
f 4 3 7 8
f 1 4 8 5
f 2 6 7 3
)OBJ";

	{
		auto cubeMeshData = std::make_shared<MeshData>();
		const bool ok = cubeMeshData->LoadObjFromString(kCubeObj);
		if (!ok)
		{
			NEURUS_ERR("[LoadMultiShadow] Failed to parse cube OBJ string");
			return res;
		}

		auto cubeMesh = std::make_shared<Mesh>();
		cubeMesh->o_name = "MultiShadowCube";
		cubeMesh->o_mesh = cubeMeshData;
		cubeMesh->UploadToGPU(device, physicalDevice, graphicsQueue, queueFamilyIndex);
		cubeMesh->SetPosition(glm::vec3(0.0f, 3.0f, 0.0f));  // raise cube above plane

		res.scene->UseMesh(cubeMesh);

		GeometryRenderItem item{};
		item.vertexBuffer = cubeMesh->GetVertexBuffer()->buffer();
		item.indexBuffer  = cubeMesh->GetIndexBuffer()->buffer();
		item.indexCount   = cubeMesh->GetGPUIndexCount();
		item.indexType    = vk::IndexType::eUint32;
		// Cube pushed to (0, 3, 0) via model matrix; geometry at origin.
		item.pushConstants.model        = cubeMesh->GetModelMatrix();
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		res.renderItems.push_back(item);
	}

	// ===================================================================
	//  2. Plane: large quad at y=0, [-10,10] in XZ, facing +Y
	//     4 vertices, 2 triangles (6 indices)
	// ===================================================================

	const char* kPlaneObj = R"OBJ(
v -10 0 -10
v 10 0 -10
v 10 0 10
v -10 0 10

f 1 4 3 2
)OBJ";

	{
		auto planeMeshData = std::make_shared<MeshData>();
		const bool ok = planeMeshData->LoadObjFromString(kPlaneObj);
		if (!ok)
		{
			NEURUS_ERR("[LoadMultiShadow] Failed to parse plane OBJ string");
			return res;
		}

		auto planeMesh = std::make_shared<Mesh>();
		planeMesh->o_name = "MultiShadowPlane";
		planeMesh->o_mesh = planeMeshData;
		planeMesh->UploadToGPU(device, physicalDevice, graphicsQueue, queueFamilyIndex);

		res.scene->UseMesh(planeMesh);

		GeometryRenderItem item{};
		item.vertexBuffer = planeMesh->GetVertexBuffer()->buffer();
		item.indexBuffer  = planeMesh->GetIndexBuffer()->buffer();
		item.indexCount   = planeMesh->GetGPUIndexCount();
		item.indexType    = vk::IndexType::eUint32;
		// Identity: plane geometry is already in world space (y=0, spans ±10 in XZ).
		item.pushConstants.model        = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		res.renderItems.push_back(item);
	}

	// ===================================================================
	//  3. N point lights at y=6, all shadow-casting, power=10, color=white.
	//
	//     Default 2 lights: A at (0, 6, 0), B at (-6, 6, 0).
	//     For other counts: distributed evenly in a circle of radius 6
	//     at y=6 so every light has a distinct shadow direction.
	// ===================================================================

	for (int i = 0; i < numLights; ++i)
	{
		auto light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
		light->o_name = "MultiShadowLight_" + std::to_string(i);

		glm::vec3 pos;
		if (numLights == 2)
		{
			// Exact positions for the default 2-light case.
			pos = (i == 0)
				? glm::vec3(0.0f, 6.0f, 0.0f)
				: glm::vec3(-6.0f, 6.0f, 0.0f);
		}
		else
		{
			// Distribute evenly on a circle at y=6, radius 6.
			const float radius = 6.0f;
			const float angle = glm::radians(
				static_cast<float>(i) * 360.0f / static_cast<float>(numLights));
			pos = glm::vec3(radius * sin(angle), 6.0f, radius * cos(angle));
		}

		light->SetPosition(pos);
		light->SetPower(10.0f);
		light->SetColor(glm::vec3(1.0f));
		light->SetShadow(true);

		res.scene->UseLight(light);
		res.lightUIDs.push_back(light->GetObjectID());
	}

	// ===================================================================
	//  4. Camera between plane (y=0) and cube (y=3), facing the plane.
	//     75deg FOV at y=2 covers +/-1.534 at plane -- full +/-1.2 shadow
	//     captured with 28% margin for extra no-shadow region.
	// ===================================================================

	{
		auto cam = std::make_shared<Camera>(256.0f, 256.0f, 75.0f, 0.1f, 100.0f);
		cam->o_name = "MultiShadowCamera";
		cam->SetCamPos(glm::vec3(0.0f, 2.0f, 0.001f));  // 0.001 Z avoids degenerate lookAt
		cam->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
		res.scene->UseCamera(cam);
	}

	NEURUS_LOG("[LoadMultiShadow] Built 2 meshes (cube + plane) + "
	           << numLights << " point lights + 1 camera -- "
	           << res.renderItems.size() << " render items");

	return res;
}

} // namespace test
} // namespace neurus
