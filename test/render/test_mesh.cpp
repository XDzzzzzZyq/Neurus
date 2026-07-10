/**
 * @file test_mesh.cpp
 * @brief GPU tests for RenderCache::GetMeshGPU() mesh upload lifecycle.
 *
 * Validates that:
 *   - GetMeshGPU lazily creates VertexBuffer and IndexBuffer
 *   - GetMeshGPU returns the same MeshGPU on subsequent calls (caching)
 *   - RemoveMeshGPU destroys GPU buffers
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "shared/TestVulkanShared.h"

#include "scene/Mesh.h"
#include "asset/MeshData.h"
#include "render/RenderCache.h"
#include "render/MeshGPU.h"

using namespace neurus;

// ---------------------------------------------------------------------------
// In-memory OBJ: a single triangle with normals
// ---------------------------------------------------------------------------

namespace
{
	const char* kTriangleObj =
		"v 0.0 0.0 0.0\n"
		"v 1.0 0.0 0.0\n"
		"v 0.0 1.0 0.0\n"
		"vn 0.0 0.0 1.0\n"
		"f 1//1 2//1 3//1\n";
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class MeshRenderTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		m_cache = std::make_unique<RenderCache>(*m_device, PhysicalDevice());
	}

	void TearDown() override
	{
		m_cache.reset();
		VulkanTestShared::TearDown();
	}

	std::unique_ptr<RenderCache> m_cache;
};

// ---------------------------------------------------------------------------
// GetMeshGPU_CreatesBuffers
// ---------------------------------------------------------------------------

/**
 * @brief After GetMeshGPU, the returned MeshGPU has non-null buffers
 *        and positive index count.
 */
TEST_F(MeshRenderTest, GetMeshGPU_CreatesBuffers)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObjFromString(kTriangleObj));

	constexpr int kTestObjectId = 42;
	MeshGPU& gpu = m_cache->GetMeshGPU(kTestObjectId, m_queue, m_graphicsQueueFamily, *meshData);

	EXPECT_NE(gpu.vertexBuffer, nullptr);
	EXPECT_NE(gpu.indexBuffer, nullptr);
	EXPECT_GT(gpu.indexCount, 0u);
}

// ---------------------------------------------------------------------------
// GetMeshGPU_ReturnsCached
// ---------------------------------------------------------------------------

/**
 * @brief Calling GetMeshGPU twice with the same objectId returns the
 *        same MeshGPU (cached, not re-created).
 */
TEST_F(MeshRenderTest, GetMeshGPU_ReturnsCached)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObjFromString(kTriangleObj));

	constexpr int kTestObjectId = 99;
	MeshGPU& first  = m_cache->GetMeshGPU(kTestObjectId, m_queue, m_graphicsQueueFamily, *meshData);
	MeshGPU& second = m_cache->GetMeshGPU(kTestObjectId, m_queue, m_graphicsQueueFamily, *meshData);

	EXPECT_EQ(&first, &second);
	EXPECT_NE(first.vertexBuffer, nullptr);
	EXPECT_NE(first.indexBuffer, nullptr);
}

// ---------------------------------------------------------------------------
// RemoveMeshGPU_Destroys
// ---------------------------------------------------------------------------

/**
 * @brief After RemoveMeshGPU, the MeshGPU entries are gone.
 */
TEST_F(MeshRenderTest, RemoveMeshGPU_Destroys)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObjFromString(kTriangleObj));

	constexpr int kTestObjectId = 77;

	// Create
	MeshGPU& created = m_cache->GetMeshGPU(kTestObjectId, m_queue, m_graphicsQueueFamily, *meshData);
	EXPECT_NE(created.vertexBuffer, nullptr);

	// Remove
	m_cache->RemoveMeshGPU(kTestObjectId);

	// Subsequent get creates a new one
	MeshGPU& recreated = m_cache->GetMeshGPU(kTestObjectId, m_queue, m_graphicsQueueFamily, *meshData);
	EXPECT_NE(recreated.vertexBuffer, nullptr);
	// Should be a different instance
	EXPECT_NE(&created, &recreated);
}

// ---------------------------------------------------------------------------
// GetMeshGPU_EmptyMeshData
// ---------------------------------------------------------------------------

/**
 * @brief GetMeshGPU with empty MeshData returns an empty MeshGPU
 *        (null buffers, zero counts) rather than crashing.
 */
TEST_F(MeshRenderTest, GetMeshGPU_EmptyMeshData)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	MeshData emptyData;
	constexpr int kTestObjectId = 0;
	MeshGPU& gpu = m_cache->GetMeshGPU(kTestObjectId, m_queue, m_graphicsQueueFamily, emptyData);

	EXPECT_EQ(gpu.vertexBuffer, nullptr);
	EXPECT_EQ(gpu.indexBuffer, nullptr);
	EXPECT_EQ(gpu.indexCount, 0u);
}
