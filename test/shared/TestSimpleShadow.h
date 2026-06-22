/**
 * @file TestSimpleShadow.h
 * @brief Header-only helper for a simple shadow-map test scene: Cube + Plane.
 *
 * Provides LoadSimpleShadow() which procedurally generates a unit cube
 * (centered at origin) and a large ground plane (y=0, [-5,5] in XZ),
 * uploads them to GPU vertex/index buffers, and returns render items
 * suitable for ShadowDepthPass.
 *
 * Usage:
 * @code
 *   auto scene = LoadSimpleShadow(device, pd, queue, qfi);
 *   m_shadowPass->SetLightPosition(scene.lightPosition);
 *   m_shadowPass->Record(cmd, ctx);  // uses scene.renderItems internally
 * @endcode
 *
 * Geometry is 100% procedural — no OBJ files needed.
 */

#pragma once

#include "Log.h"
#include "render/passes/GeometryPass.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace neurus {
namespace test {

// ---------------------------------------------------------------------------
// Test vertex structure (matches ShadowDepthPass::BufferLayout:
//   pos(3) at offset 0  → eR32G32B32Sfloat
//   nrm(3) at offset 12 → eR32G32B32Sfloat
//   uv(2)  at offset 24 → eR32G32Sfloat)
// ---------------------------------------------------------------------------
struct SimpleVertex
{
	float posX, posY, posZ;
	float nrmX, nrmY, nrmZ;
	float uvX,  uvY;
};
static_assert(sizeof(SimpleVertex) == 32,
              "SimpleVertex must be 32 bytes (matches ShadowDepthPass BufferLayout)");

/**
 * @brief Per-mesh entry: owns the GPU buffers and exposes the render item.
 */
struct SimpleShadowMeshEntry
{
	std::unique_ptr<VertexBuffer> vertexBuffer;
	std::unique_ptr<IndexBuffer>  indexBuffer;
	GeometryRenderItem            renderItem = {};
};

/**
 * @brief Aggregated result of building the simple shadow test scene.
 *
 * All GPU buffers are owned by this struct (via meshes vector).
 * renderItems provides a flat list for direct use with ShadowDepthPass.
 */
struct SimpleShadowResources
{
	std::vector<SimpleShadowMeshEntry> meshes;
	std::vector<GeometryRenderItem>    renderItems;

	glm::vec3 lightPosition;
};

/**
 * @brief Builds the simple shadow-map test scene procedurally.
 *
 * Generates:
 *   - A unit cube (12 triangles, 24 vertices, 36 indices) centered at origin,
 *     with flat-shaded per-face normals.
 *   - A ground-plane quad (2 triangles, 4 vertices, 6 indices) at y=0,
 *     spanning [-5,5] in XZ, facing +Y.
 *
 * Both meshes use identity model matrices (geometry is already in world space).
 * The light is positioned at (0, 3, 0) — directly above the cube, casting
 * shadows downward onto the plane.
 *
 * @note All buffers use device-local memory. A staging upload is performed
 *       synchronously on the provided graphics queue.
 *
 * @param device           Logical device.
 * @param physicalDevice   Physical device (for memory properties).
 * @param graphicsQueue    Graphics queue for staging uploads.
 * @param queueFamilyIndex Queue family index for VulkanBuffer creation.
 * @return Fully populated SimpleShadowResources (move-only due to unique_ptr members).
 */
inline SimpleShadowResources LoadSimpleShadow(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue graphicsQueue,
	uint32_t queueFamilyIndex)
{
	SimpleShadowResources res;

	// ===================================================================
	//  1. Cube: unit cube centered at origin [-0.5, +0.5]³
	//     24 unique vertices (4 per face, flat normals)
	//     36 indices (6 per face, triangle list, CCW from outside)
	// ===================================================================

	// clang-format off
	const std::vector<SimpleVertex> cubeVertices = {
		// --- Front (+Z), normal=( 0, 0, 1) ---
		{-0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f},  // 0: bottom-left
		{ 0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f},  // 1: bottom-right
		{ 0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f},  // 2: top-right
		{-0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f},  // 3: top-left

		// --- Back (-Z),  normal=( 0, 0,-1) ---
		{ 0.5f, -0.5f, -0.5f,  0.0f, 0.0f,-1.0f,  0.0f, 0.0f},  // 4: bottom-left (from back)
		{-0.5f, -0.5f, -0.5f,  0.0f, 0.0f,-1.0f,  1.0f, 0.0f},  // 5: bottom-right
		{-0.5f,  0.5f, -0.5f,  0.0f, 0.0f,-1.0f,  1.0f, 1.0f},  // 6: top-right
		{ 0.5f,  0.5f, -0.5f,  0.0f, 0.0f,-1.0f,  0.0f, 1.0f},  // 7: top-left

		// --- Right (+X), normal=( 1, 0, 0) ---
		{ 0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f},  // 8: bottom-left (from right)
		{ 0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f},  // 9: bottom-right
		{ 0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  1.0f, 1.0f},  // 10: top-right
		{ 0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f},  // 11: top-left

		// --- Left (-X),  normal=(-1, 0, 0) ---
		{-0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f,  0.0f, 0.0f},  // 12: bottom-left (from left)
		{-0.5f, -0.5f,  0.5f, -1.0f, 0.0f, 0.0f,  1.0f, 0.0f},  // 13: bottom-right
		{-0.5f,  0.5f,  0.5f, -1.0f, 0.0f, 0.0f,  1.0f, 1.0f},  // 14: top-right
		{-0.5f,  0.5f, -0.5f, -1.0f, 0.0f, 0.0f,  0.0f, 1.0f},  // 15: top-left

		// --- Top (+Y),   normal=( 0, 1, 0) ---
		{-0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f},  // 16: bottom-left (from top)
		{ 0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f},  // 17: bottom-right
		{ 0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f},  // 18: top-right
		{-0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f},  // 19: top-left

		// --- Bottom (-Y), normal=( 0,-1, 0) ---
		{-0.5f, -0.5f, -0.5f,  0.0f,-1.0f, 0.0f,  0.0f, 0.0f},  // 20: bottom-left (from below)
		{ 0.5f, -0.5f, -0.5f,  0.0f,-1.0f, 0.0f,  1.0f, 0.0f},  // 21: bottom-right
		{ 0.5f, -0.5f,  0.5f,  0.0f,-1.0f, 0.0f,  1.0f, 1.0f},  // 22: top-right
		{-0.5f, -0.5f,  0.5f,  0.0f,-1.0f, 0.0f,  0.0f, 1.0f},  // 23: top-left
	};

	const std::vector<uint32_t> cubeIndices = {
		// Front (+Z): 0,1,2, 0,2,3
		 0,  1,  2,   0,  2,  3,
		// Back (-Z):  4,5,6, 4,6,7
		 4,  5,  6,   4,  6,  7,
		// Right (+X): 8,9,10, 8,10,11
		 8,  9, 10,   8, 10, 11,
		// Left (-X):  12,13,14, 12,14,15
		12, 13, 14,  12, 14, 15,
		// Top (+Y):   16,17,18, 16,18,19
		16, 17, 18,  16, 18, 19,
		// Bottom (-Y):20,21,22, 20,22,23
		20, 21, 22,  20, 22, 23,
	};
	// clang-format on

	// ===================================================================
	//  2. Plane: large quad at y=0, [-5,5] in XZ, facing +Y
	//     4 vertices, 6 indices (2 triangles, CCW from above)
	// ===================================================================

	// clang-format off
	const std::vector<SimpleVertex> planeVertices = {
		{-5.0f, 0.0f, -5.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f},  // 0: bottom-left
		{ 5.0f, 0.0f, -5.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f},  // 1: bottom-right
		{ 5.0f, 0.0f,  5.0f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f},  // 2: top-right
		{-5.0f, 0.0f,  5.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f},  // 3: top-left
	};

	// CCW from above (+Y): 0,2,1, 0,3,2
	const std::vector<uint32_t> planeIndices = {
		0, 2, 1,   0, 3, 2,
	};
	// clang-format on

	// ===================================================================
	//  3. Upload to GPU and build render items
	// ===================================================================

	res.meshes.reserve(2);
	res.renderItems.reserve(2);

	// --- Cube ---
	{
		SimpleShadowMeshEntry entry;

		entry.vertexBuffer = std::make_unique<VertexBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			cubeVertices.data(),
			cubeVertices.size() * sizeof(SimpleVertex),
			sizeof(SimpleVertex),
			static_cast<uint32_t>(cubeVertices.size()),
			"SimpleShadowCube");

		entry.indexBuffer = std::make_unique<IndexBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			cubeIndices.data(),
			cubeIndices.size() * sizeof(uint32_t),
			static_cast<uint32_t>(cubeIndices.size()),
			"SimpleShadowCube");

		GeometryRenderItem& item = entry.renderItem;
		item.vertexBuffer = entry.vertexBuffer->buffer();
		item.indexBuffer  = entry.indexBuffer->buffer();
		item.indexCount   = entry.indexBuffer->GetIndexCount();
		item.indexType    = entry.indexBuffer->GetIndexType();
		// Identity: cube geometry is already in world space at origin.
		item.pushConstants.model = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		res.meshes.push_back(std::move(entry));
		res.renderItems.push_back(res.meshes.back().renderItem);
	}

	// --- Plane ---
	{
		SimpleShadowMeshEntry entry;

		entry.vertexBuffer = std::make_unique<VertexBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			planeVertices.data(),
			planeVertices.size() * sizeof(SimpleVertex),
			sizeof(SimpleVertex),
			static_cast<uint32_t>(planeVertices.size()),
			"SimpleShadowPlane");

		entry.indexBuffer = std::make_unique<IndexBuffer>(
			device, physicalDevice, graphicsQueue, queueFamilyIndex,
			planeIndices.data(),
			planeIndices.size() * sizeof(uint32_t),
			static_cast<uint32_t>(planeIndices.size()),
			"SimpleShadowPlane");

		GeometryRenderItem& item = entry.renderItem;
		item.vertexBuffer = entry.vertexBuffer->buffer();
		item.indexBuffer  = entry.indexBuffer->buffer();
		item.indexCount   = entry.indexBuffer->GetIndexCount();
		item.indexType    = entry.indexBuffer->GetIndexType();
		// Identity: plane geometry is already in world space (y=0, spans ±5 in XZ).
		item.pushConstants.model = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		res.meshes.push_back(std::move(entry));
		res.renderItems.push_back(res.meshes.back().renderItem);
	}

	// ===================================================================
	//  4. Light position
	//     Above the cube at (0, 3, 0), casting shadows downward onto the plane.
	// ===================================================================
	res.lightPosition = glm::vec3(0.0f, 3.0f, 0.0f);

	NEURUS_LOG("[LoadSimpleShadow] Built 2 meshes (cube + plane) — "
	           << cubeVertices.size() << " cube vertices, "
	           << planeVertices.size() << " plane vertices");

	return res;
}

} // namespace test
} // namespace neurus
