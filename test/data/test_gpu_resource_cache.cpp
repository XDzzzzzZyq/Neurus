/**
 * @file test_gpu_resource_cache.cpp
 * @brief TDD tests for GPUResourceCache — mesh buffer upload/query/remove/clear and light SSBO management.
 *
 * Validates:
 *   - UploadMesh stores vertex/index buffers and GetVertexBuffer/GetIndexBuffer return valid pointers
 *   - Query for non-existent mesh ID returns nullptr
 *   - UploadLights creates light SSBO from scene point lights
 *   - Empty scene → GetLightSSBO returns nullptr, GetLightCount returns 0
 *   - RemoveMesh cleans up buffers for a specific mesh
 *   - Clear releases all buffers (mesh + light)
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanFixture.h"
#include "data/GPUResourceCache.h"
#include "data/MeshData.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "scene/Light.h"

#include <memory>
#include <string>

using namespace neurus;

// ---------------------------------------------------------------------------
// Test OBJ string: simple 3-vertex triangle
// ---------------------------------------------------------------------------

static const char* kTriangleObj =
	"# Simple triangle with normals and UVs\n"
	"v 0.0 0.5 0.0\n"
	"v -0.5 -0.5 0.0\n"
	"v 0.5 -0.5 0.0\n"
	"vn 0.0 0.0 1.0\n"
	"vt 0.5 1.0\n"
	"vt 0.0 0.0\n"
	"vt 1.0 0.0\n"
	"f 1/1/1 2/2/1 3/3/1\n";

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @brief GPU test fixture for GPUResourceCache.
 *
 * Creates a headless Vulkan device and queue on SetUp, then constructs
 * GPUResourceCache with borrowed device handles. No surface or swapchain needed.
 */
class GPUResourceCacheTest : public VulkanTestFixture
{
protected:
	void SetUp() override
	{
		VulkanTestFixture::SetUp();
		if (!m_hasVulkan) return;

		// GPUResourceCache is created lazily by individual tests
	}

	void TearDown() override
	{
		// GPUResourceCache must be destroyed before device
		m_cache.reset();
		VulkanTestFixture::TearDown();
	}

	/**
	 * @brief Creates a GPUResourceCache using the fixture's Vulkan handles.
	 */
	void CreateCache()
	{
		m_cache = std::make_unique<GPUResourceCache>(
			*m_device,
			m_physicalDevices[m_selectedPdIndex],
			m_queue,
			m_graphicsQueueFamily);
	}

	/**
	 * @brief Creates a Mesh with triangle MeshData loaded from kTriangleObj.
	 */
	std::shared_ptr<Mesh> CreateTriangleMesh()
	{
		auto md = std::make_shared<MeshData>();
		EXPECT_TRUE(md->LoadObjFromString(kTriangleObj));

		auto mesh = std::make_shared<Mesh>();
		mesh->o_mesh = md;
		return mesh;
	}

	/**
	 * @brief Creates a Scene with one POINTLIGHT registered.
	 */
	std::unique_ptr<Scene> CreateSceneWithLight()
	{
		auto scene = std::make_unique<Scene>();
		auto light = std::make_shared<Light>(LightType::POINTLIGHT, 50.0f, glm::vec3(1.0f, 0.5f, 0.2f));
		light->SetPosition(glm::vec3(5.0f, 3.0f, -2.0f));
		scene->UseLight(light);
		return scene;
	}

	std::unique_ptr<GPUResourceCache> m_cache;
};

// ---------------------------------------------------------------------------
// 1. UploadMesh → GetVertexBuffer returns valid pointer
// ---------------------------------------------------------------------------

/**
 * @test UploadMesh creates GPU vertex buffer and GetVertexBuffer returns a non-null pointer.
 */
TEST_F(GPUResourceCacheTest, UploadMesh_ThenGetVertexBuffer_ReturnsValidPointer)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CreateCache();
	ASSERT_NE(m_cache, nullptr);

	auto mesh = CreateTriangleMesh();
	ASSERT_NE(mesh->o_mesh, nullptr);
	const int meshID = mesh->GetObjectID();

	// Upload
	m_cache->UploadMesh(*mesh);

	// Query vertex buffer
	const VertexBuffer* vbo = m_cache->GetVertexBuffer(meshID);
	ASSERT_NE(vbo, nullptr);

	// Query index buffer
	const IndexBuffer* ibo = m_cache->GetIndexBuffer(meshID);
	ASSERT_NE(ibo, nullptr);

	// Index count should be non-zero
	const uint32_t idxCount = m_cache->GetIndexCount(meshID);
	EXPECT_GT(idxCount, 0u);
}

// ---------------------------------------------------------------------------
// 2. GetVertexBuffer for non-existent mesh returns nullptr
// ---------------------------------------------------------------------------

/**
 * @test Querying a mesh ID that was never uploaded returns nullptr for all getters.
 */
TEST_F(GPUResourceCacheTest, GetVertexBuffer_NonexistentMesh_ReturnsNull)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CreateCache();

	// Query a non-existent mesh ID
	const int fakeID = 99999;
	EXPECT_EQ(m_cache->GetVertexBuffer(fakeID), nullptr);
	EXPECT_EQ(m_cache->GetIndexBuffer(fakeID), nullptr);
	EXPECT_EQ(m_cache->GetIndexCount(fakeID), 0u);
}

// ---------------------------------------------------------------------------
// 3. UploadLights → GetLightSSBO returns valid pointer
// ---------------------------------------------------------------------------

/**
 * @test Uploading a scene with a POINTLIGHT creates the light SSBO and GetLightSSBO returns non-null.
 */
TEST_F(GPUResourceCacheTest, UploadLights_ThenGetLightSSBO_ReturnsValidPointer)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CreateCache();

	auto scene = CreateSceneWithLight();
	ASSERT_EQ(scene->light_list.size(), 1u);

	m_cache->UploadLights(*scene);

	const VulkanBuffer* ssbo = m_cache->GetLightSSBO();
	ASSERT_NE(ssbo, nullptr);

	const uint32_t lightCount = m_cache->GetLightCount();
	EXPECT_EQ(lightCount, 1u);
}

// ---------------------------------------------------------------------------
// 4. ZeroLights → GetLightSSBO returns nullptr
// ---------------------------------------------------------------------------

/**
 * @test Uploading an empty scene (no lights) releases the SSBO and GetLightSSBO returns nullptr.
 */
TEST_F(GPUResourceCacheTest, ZeroLights_GetLightSSBO_ReturnsNull)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CreateCache();

	Scene emptyScene;

	m_cache->UploadLights(emptyScene);

	EXPECT_EQ(m_cache->GetLightSSBO(), nullptr);
	EXPECT_EQ(m_cache->GetLightCount(), 0u);
}

// ---------------------------------------------------------------------------
// 5. RemoveMesh → buffer cleaned up
// ---------------------------------------------------------------------------

/**
 * @test After RemoveMesh, GetVertexBuffer and GetIndexBuffer return nullptr for that mesh.
 */
TEST_F(GPUResourceCacheTest, RemoveMesh_BufferCleanedUp)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CreateCache();

	auto mesh = CreateTriangleMesh();
	const int meshID = mesh->GetObjectID();

	// Upload
	m_cache->UploadMesh(*mesh);

	// Verify buffers are present
	EXPECT_NE(m_cache->GetVertexBuffer(meshID), nullptr);
	EXPECT_NE(m_cache->GetIndexBuffer(meshID), nullptr);

	// Remove
	m_cache->RemoveMesh(meshID);

	// Verify buffers are gone
	EXPECT_EQ(m_cache->GetVertexBuffer(meshID), nullptr);
	EXPECT_EQ(m_cache->GetIndexBuffer(meshID), nullptr);
	EXPECT_EQ(m_cache->GetIndexCount(meshID), 0u);
}

// ---------------------------------------------------------------------------
// 6. Clear → all buffers released
// ---------------------------------------------------------------------------

/**
 * @test Clear releases both mesh buffers and light SSBO; all getters return nullptr.
 */
TEST_F(GPUResourceCacheTest, Clear_AllBuffersReleased)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	CreateCache();

	// Upload a mesh
	auto mesh = CreateTriangleMesh();
	const int meshID = mesh->GetObjectID();
	m_cache->UploadMesh(*mesh);

	// Upload lights
	auto scene = CreateSceneWithLight();
	m_cache->UploadLights(*scene);

	// Verify everything is present
	EXPECT_NE(m_cache->GetVertexBuffer(meshID), nullptr);
	EXPECT_NE(m_cache->GetLightSSBO(), nullptr);

	// Clear all
	m_cache->Clear();

	// Everything should be null
	EXPECT_EQ(m_cache->GetVertexBuffer(meshID), nullptr);
	EXPECT_EQ(m_cache->GetIndexBuffer(meshID), nullptr);
	EXPECT_EQ(m_cache->GetIndexCount(meshID), 0u);
	EXPECT_EQ(m_cache->GetLightSSBO(), nullptr);
	EXPECT_EQ(m_cache->GetLightCount(), 0u);
}
