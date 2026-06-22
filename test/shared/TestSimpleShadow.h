/**
 * @file TestSimpleShadow.h
 * @brief Header-only helper for a simple shadow-map test scene: Cube + Plane.
 *
 * Provides LoadSimpleShadow() which procedurally generates a unit cube
 * (centered at origin) and a large ground plane (y=0, [-5,5] in XZ),
 * uploads them to GPU via Scene/Mesh/Light, and returns render items
 * suitable for ShadowDepthPass.
 *
 * Usage:
 * @code
 *   auto scene = LoadSimpleShadow(device, pd, queue, qfi);
 *   m_shadowPass->SetLightPosition(scene.lightPosition);
 *   m_shadowPass->Record(cmd, ctx);  // uses scene.renderItems internally
 * @endcode
 *
 * Geometry is 100% procedural via OBJ strings — no OBJ files needed.
 */

#pragma once

#include "Log.h"
#include "scene/Scene.h"
#include "scene/Mesh.h"
#include "scene/Light.h"
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
 * @brief Aggregated result of building the simple shadow test scene.
 *
 * The Scene owns all GPU resources (meshes hold VertexBuffer/IndexBuffer
 * internally).  renderItems provides a flat list for direct use with
 * ShadowDepthPass.
 */
struct SimpleShadowResources
{
	std::shared_ptr<Scene> scene;
	std::vector<GeometryRenderItem> renderItems;

	glm::vec3 lightPosition{0.0f, 3.0f, 0.0f};
};

/**
 * @brief Builds the simple shadow-map test scene procedurally.
 *
 * Generates:
 *   - A unit cube (12 triangles, 8 vertices, 36 indices) centered at origin,
 *     covering [-0.5, +0.5]^3.
 *   - A ground-plane quad (2 triangles, 4 vertices, 6 indices) at y=0,
 *     spanning [-5,5] in XZ, facing +Y.
 *
 * Both meshes use identity model matrices (geometry is already in world space).
 * The light is positioned at (0, 3, 0) — directly above the cube, casting
 * shadows downward onto the plane.
 *
 * @note All buffers use device-local memory.  A staging upload is performed
 *       synchronously on the provided graphics queue via Mesh::UploadToGPU().
 *
 * @param device           Logical device.
 * @param physicalDevice   Physical device (for memory properties).
 * @param graphicsQueue    Graphics queue for staging uploads.
 * @param queueFamilyIndex Queue family index for VulkanBuffer creation.
 * @return Fully populated SimpleShadowResources.
 */
inline SimpleShadowResources LoadSimpleShadow(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue graphicsQueue,
	uint32_t queueFamilyIndex)
{
	SimpleShadowResources res;
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
			NEURUS_ERR("[LoadSimpleShadow] Failed to parse cube OBJ string");
			return res;
		}

		auto cubeMesh = std::make_shared<Mesh>();
		cubeMesh->o_name = "SimpleShadowCube";
		cubeMesh->o_mesh = cubeMeshData;
		cubeMesh->UploadToGPU(device, physicalDevice, graphicsQueue, queueFamilyIndex);

		res.scene->UseMesh(cubeMesh);

		GeometryRenderItem item{};
		item.vertexBuffer = cubeMesh->GetVertexBuffer()->buffer();
		item.indexBuffer  = cubeMesh->GetIndexBuffer()->buffer();
		item.indexCount   = cubeMesh->GetGPUIndexCount();
		item.indexType    = vk::IndexType::eUint32;
		// Identity: cube geometry is already in world space at origin.
		item.pushConstants.model        = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		res.renderItems.push_back(item);
	}

	// ===================================================================
	//  2. Plane: large quad at y=0, [-5,5] in XZ, facing +Y
	//     4 vertices, 2 triangles (6 indices)
	// ===================================================================

	const char* kPlaneObj = R"OBJ(
v -5 0 -5
v 5 0 -5
v 5 0 5
v -5 0 5

f 1 2 3 4
)OBJ";

	{
		auto planeMeshData = std::make_shared<MeshData>();
		const bool ok = planeMeshData->LoadObjFromString(kPlaneObj);
		if (!ok)
		{
			NEURUS_ERR("[LoadSimpleShadow] Failed to parse plane OBJ string");
			return res;
		}

		auto planeMesh = std::make_shared<Mesh>();
		planeMesh->o_name = "SimpleShadowPlane";
		planeMesh->o_mesh = planeMeshData;
		planeMesh->UploadToGPU(device, physicalDevice, graphicsQueue, queueFamilyIndex);

		res.scene->UseMesh(planeMesh);

		GeometryRenderItem item{};
		item.vertexBuffer = planeMesh->GetVertexBuffer()->buffer();
		item.indexBuffer  = planeMesh->GetIndexBuffer()->buffer();
		item.indexCount   = planeMesh->GetGPUIndexCount();
		item.indexType    = vk::IndexType::eUint32;
		// Identity: plane geometry is already in world space (y=0, spans ±5 in XZ).
		item.pushConstants.model        = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		res.renderItems.push_back(item);
	}

	// ===================================================================
	//  3. Point light at (0, 3, 0) — above the cube, casting shadows
	//     downward onto the plane.
	// ===================================================================

	{
		auto light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
		light->o_name = "SimpleShadowLight";
		light->SetPosition(res.lightPosition);
		res.scene->UseLight(light);
	}

	NEURUS_LOG("[LoadSimpleShadow] Built 2 meshes (cube + plane) + 1 point light — "
	           << res.renderItems.size() << " render items");

	return res;
}

} // namespace test
} // namespace neurus
