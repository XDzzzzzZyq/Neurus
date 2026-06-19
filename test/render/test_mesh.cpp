/**
 * @file test_mesh.cpp
 * @brief GPU tests for Mesh::UploadToGPU() / ReleaseGPUBuffers() lifecycle.
 *
 * Validates that:
 *   - UploadToGPU creates valid VertexBuffer and IndexBuffer
 *   - ReleaseGPUBuffers destroys GPU buffers (null after release)
 *   - Mesh destruction after UploadToGPU does NOT trigger
 *     VUID-vkFreeMemory-memory-00677 (destructor calls waitIdle first)
 *   - Upload → Release → Re-upload cycle works (m_gpuDevice updated)
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
#include "render/buffers/VertexBuffer.h"
#include "render/buffers/IndexBuffer.h"

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
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}
};

// ---------------------------------------------------------------------------
// UploadToGPU_CreatesBuffers
// ---------------------------------------------------------------------------

/**
 * @brief After UploadToGPU, GetVertexBuffer / GetIndexBuffer return non-null
 *        and GetGPUIndexCount reports a positive count.
 */
TEST_F(MeshRenderTest, UploadToGPU_CreatesBuffers)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObjFromString(kTriangleObj));

	Mesh mesh;
	mesh.o_mesh = meshData;

	mesh.UploadToGPU(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);

	EXPECT_NE(mesh.GetVertexBuffer(), nullptr);
	EXPECT_NE(mesh.GetIndexBuffer(), nullptr);
	EXPECT_GT(mesh.GetGPUIndexCount(), 0u);
}

// ---------------------------------------------------------------------------
// ReleaseGPUBuffers_DestroysBuffers
// ---------------------------------------------------------------------------

/**
 * @brief After ReleaseGPUBuffers, GetVertexBuffer / GetIndexBuffer return
 *        nullptr and GetGPUIndexCount is zero.
 */
TEST_F(MeshRenderTest, ReleaseGPUBuffers_DestroysBuffers)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObjFromString(kTriangleObj));

	Mesh mesh;
	mesh.o_mesh = meshData;

	mesh.UploadToGPU(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);
	mesh.ReleaseGPUBuffers();

	EXPECT_EQ(mesh.GetVertexBuffer(), nullptr);
	EXPECT_EQ(mesh.GetIndexBuffer(), nullptr);
	EXPECT_EQ(mesh.GetGPUIndexCount(), 0u);
}

// ---------------------------------------------------------------------------
// MeshDestructionAfterUpload
// ---------------------------------------------------------------------------

/**
 * @brief Mesh destroyed after UploadToGPU (no explicit ReleaseGPUBuffers).
 *        The destructor calls ReleaseGPUBuffers which calls
 *        m_gpuDevice->waitIdle() before freeing — no VUID-vkFreeMemory-00677.
 */
TEST_F(MeshRenderTest, MeshDestructionAfterUpload)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObjFromString(kTriangleObj));

	{
		Mesh mesh;
		mesh.o_mesh = meshData;

		mesh.UploadToGPU(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);

		EXPECT_NE(mesh.GetVertexBuffer(), nullptr);
		EXPECT_NE(mesh.GetIndexBuffer(), nullptr);
		EXPECT_GT(mesh.GetGPUIndexCount(), 0u);

		// mesh goes out of scope → destructor calls ReleaseGPUBuffers
		//   → waitIdle() → m_gpuVertices.reset() / m_gpuIndices.reset()
	}

	// Reaching here without Vulkan validation errors means the fix works.
	SUCCEED();
}

// ---------------------------------------------------------------------------
// MeshUploadReleaseReupload
// ---------------------------------------------------------------------------

/**
 * @brief Upload → Release → Upload again. Verifies that m_gpuDevice is
 *        updated on re-upload and the second upload produces valid buffers.
 */
TEST_F(MeshRenderTest, MeshUploadReleaseReupload)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";

	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObjFromString(kTriangleObj));

	Mesh mesh;
	mesh.o_mesh = meshData;

	// --- First upload ---
	mesh.UploadToGPU(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);

	EXPECT_NE(mesh.GetVertexBuffer(), nullptr);
	EXPECT_NE(mesh.GetIndexBuffer(), nullptr);
	EXPECT_GT(mesh.GetGPUIndexCount(), 0u);

	// --- Release ---
	mesh.ReleaseGPUBuffers();

	EXPECT_EQ(mesh.GetVertexBuffer(), nullptr);
	EXPECT_EQ(mesh.GetIndexBuffer(), nullptr);
	EXPECT_EQ(mesh.GetGPUIndexCount(), 0u);

	// --- Re-upload ---
	mesh.UploadToGPU(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);

	EXPECT_NE(mesh.GetVertexBuffer(), nullptr);
	EXPECT_NE(mesh.GetIndexBuffer(), nullptr);
	EXPECT_GT(mesh.GetGPUIndexCount(), 0u);
}
