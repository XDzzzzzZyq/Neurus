/**
 * @file TestMultiShadow.h
 * @brief Header-only parametrized cube-on-plane scene builder with N point lights.
 *
 * Provides LoadMultiShadow() which procedurally generates a unit cube
 * (centered at origin, positioned at (0,0,0)) and a large
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
 *     covering [-0.5, +0.5]^3, positioned at (0,0,0) (resting on the plane).
 *   - A ground-plane quad (2 triangles, 4 vertices, 6 indices) at y=0,
 *     spanning [-10,10] in XZ, facing +Y.
 *
 * The cube and plane both use identity model matrices (cube geometry at origin,
 * plane geometry at y=0).  Lights are placed on a circle of radius 2 at y=2.
 * For other counts they are distributed evenly at angles 0°, 360°/N, etc.
 *
 * All lights have power=3, color=white, and shadow=true.
 *
 * @note All buffers use device-local memory.  A staging upload is performed
 *       synchronously on the provided graphics queue via Mesh::UploadToGPU().
 *
 * @param device           Logical device.
 * @param physicalDevice   Physical device (for memory properties).
 * @param graphicsQueue    Graphics queue for staging uploads.
 * @param queueFamilyIndex Queue family index for GPUBuffer creation.
 * @param numLights        Number of shadow-casting point lights (default: 3).
 * @return Fully populated MultiShadowResources with scene, renderItems,
 *         and lightUIDs.
 */
inline MultiShadowResources LoadMultiShadow(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue graphicsQueue,
	uint32_t queueFamilyIndex,
	int numLights = 3)
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
		cubeMesh->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));  // cube rests on plane

		res.scene->UseMesh(cubeMesh);

		GeometryRenderItem item{};
		item.vertexBuffer = cubeMesh->GetVertexBuffer()->buffer();
		item.indexBuffer  = cubeMesh->GetIndexBuffer()->buffer();
		item.indexCount   = cubeMesh->GetGPUIndexCount();
		item.indexType    = vk::IndexType::eUint32;
		// Cube identity transform; geometry at origin resting on plane.
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
	//  3. N point lights on a circle at y=2, radius r=2, all shadow-casting,
	//     power=3, color=white.
	//
	//     Lights are placed above the cube (at y=2 vs cube at y=0) so shadows
	//     project downward onto the plane.  With the default count of 3 the
	//     lights are at (±2, 2, 0).  For other counts they are distributed
	//     evenly around the circle.  Camera at (0, 3, 1) provides a good
	//     view of the cube + plane + shadows.
	// ===================================================================

	for (int i = 0; i < numLights; ++i)
	{
		auto light = std::make_shared<Light>(LightType::POINTLIGHT, 3.0f, glm::vec3(1.0f));
		light->o_name = "MultiShadowLight_" + std::to_string(i);

		// Distribute evenly on a circle at y=2, radius 2.
		const float radius = 2.0f;
		const float angle = glm::radians(
			static_cast<float>(i) * 360.0f / static_cast<float>(numLights));
		glm::vec3 pos(radius * cos(angle), 2.0f, radius * sin(angle));

		light->SetPosition(pos);
		light->SetPower(3.0f);
		light->SetRadius(0.5f);  // 0.5 radius for soft penumbra edges
		light->SetColor(glm::vec3(1.0f));
		light->SetShadow(true);

		res.scene->UseLight(light);
		res.lightUIDs.push_back(light->GetObjectID());
	}

	// ===================================================================
	//  4. Camera at (0, 3, 1) looking at origin — provides a clear view
	//     of the cube(shadow caster) and the plane below with shadows.
	// ===================================================================

	{
		auto cam = std::make_shared<Camera>(256.0f, 256.0f, 75.0f, 0.1f, 100.0f);
		cam->o_name = "MultiShadowCamera";
		cam->SetCamPos(glm::vec3(0.0f, 3.0f, 1.0f));  // 0.001 Z avoids degenerate lookAt
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
