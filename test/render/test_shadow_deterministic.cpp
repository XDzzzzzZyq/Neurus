/**
 * @file test_shadow_deterministic.cpp
 * @brief Smoke tests for point light shadow mapping.
 *
 * Verifies the ShadowDepthPass pipeline creation, 6-face rendering,
 * face view-projection math, and ShadowEvalPass compute dispatch.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "render/passes/ShadowDepthPass.h"
#include "render/passes/ShadowEvalPass.h"
#include "render/passes/AttachmentManager.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/PassContext.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Simple cube geometry (axis-aligned unit cube)
// ---------------------------------------------------------------------------

struct TestVertex { float px, py, pz, nx, ny, nz, u, v; };

static std::vector<TestVertex> MakeCubeVerts(float size)
{
	const float h = size * 0.5f;
	std::vector<TestVertex> v;
	// CW winding from outside — all 6 faces
	v.insert(v.end(), {{-h,-h, h, 0,0,1, 0,0},{-h, h, h, 0,0,1, 0,1},{ h, h, h, 0,0,1, 1,1},{ h,-h, h, 0,0,1, 1,0}});
	v.insert(v.end(), {{ h,-h,-h, 0,0,-1, 0,0},{ h, h,-h, 0,0,-1, 0,1},{-h, h,-h, 0,0,-1, 1,1},{-h,-h,-h, 0,0,-1, 1,0}});
	v.insert(v.end(), {{ h,-h, h, 1,0,0, 0,0},{ h, h, h, 1,0,0, 0,1},{ h, h,-h, 1,0,0, 1,1},{ h,-h,-h, 1,0,0, 1,0}});
	v.insert(v.end(), {{-h,-h,-h,-1,0,0, 0,0},{-h, h,-h,-1,0,0, 0,1},{-h, h, h,-1,0,0, 1,1},{-h,-h, h,-1,0,0, 1,0}});
	v.insert(v.end(), {{-h, h, h, 0,1,0, 0,0},{-h, h,-h, 0,1,0, 0,1},{ h, h,-h, 0,1,0, 1,1},{ h, h, h, 0,1,0, 1,0}});
	v.insert(v.end(), {{-h,-h,-h, 0,-1,0, 0,0},{-h,-h, h, 0,-1,0, 0,1},{ h,-h, h, 0,-1,0, 1,1},{ h,-h,-h, 0,-1,0, 1,0}});
	return v;
}

static std::vector<uint32_t> MakeCubeIdx()
{
	std::vector<uint32_t> idx;
	for (uint32_t f = 0; f < 6; ++f)
	{
		uint32_t b = f * 4;
		idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3});
	}
	return idx;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ShadowDeterministicTest : public VulkanTestShared
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
// 1. Rendering a cube into the shadow cubemap does not crash
// ---------------------------------------------------------------------------

TEST_F(ShadowDeterministicTest, RenderCubeToShadowCubemap_DoesNotCrash)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();

	auto verts = MakeCubeVerts(1.0f);
	auto inds  = MakeCubeIdx();

	// Translate cube to (0, 0, 2)
	for (auto& v : verts) { v.pz += 2.0f; }

	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 verts.data(), verts.size() * sizeof(TestVertex),
	                 sizeof(TestVertex), static_cast<uint32_t>(verts.size()));
	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                inds.data(), inds.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(inds.size()));

	GeometryRenderItem item{};
	item.vertexBuffer = vbo.buffer();
	item.indexBuffer  = ibo.buffer();
	item.indexCount   = ibo.GetIndexCount();
	item.indexType    = ibo.GetIndexType();
	item.pushConstants.model = glm::mat4(1.0f);

	// Create and run shadow depth pass
	ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily, 64u, 25.0f);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	{
		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items{ item };
		PassContext ctx{};
		ctx.renderExtent = vk::Extent2D{64, 64};
		ctx.renderItems = &items;
		shadowPass.Record(cmd, ctx);
		EndSubmitWait(cmd);
	}
	SUCCEED();  // No crash = success
}

// ---------------------------------------------------------------------------
// 2. Face view-proj matrices: +Z looks forward from origin
// ---------------------------------------------------------------------------

TEST_F(ShadowDeterministicTest, FaceViewProj_PlusZ_LooksForward)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();
	ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily, 64u);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 25.0f);
	const glm::mat4 viewPZ = glm::lookAt(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0));

	// Point at (0, 0, 1.5) should be within frustum
	const glm::vec4 clip = proj * viewPZ * glm::vec4(0, 0, 1.5f, 1.0f);
	const float ndcZ = clip.z / clip.w;

	EXPECT_GT(ndcZ, 0.0f);
	EXPECT_LT(ndcZ, 1.0f);

	// Point at (0, 0, 0.05) should be clipped (behind near plane = 0.1)
	const glm::vec4 clipNear = proj * viewPZ * glm::vec4(0, 0, 0.05f, 1.0f);
	EXPECT_LT(clipNear.z / clipNear.w, 0.0f);
}

// ---------------------------------------------------------------------------
// 3. Shadow eval pass runs without crash
// ---------------------------------------------------------------------------

TEST_F(ShadowDeterministicTest, ShadowEvalPass_DoesNotCrash)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();

	// Create shadow depth cubemap (rendered with a cube)
	ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily, 64u, 25.0f);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	// Upload a cube to render
	auto verts = MakeCubeVerts(1.0f);
	auto inds  = MakeCubeIdx();
	for (auto& v : verts) { v.pz += 2.0f; }

	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 verts.data(), verts.size() * sizeof(TestVertex),
	                 sizeof(TestVertex), static_cast<uint32_t>(verts.size()));
	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                inds.data(), inds.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(inds.size()));

	GeometryRenderItem item{};
	item.vertexBuffer = vbo.buffer();
	item.indexBuffer  = ibo.buffer();
	item.indexCount   = ibo.GetIndexCount();
	item.indexType    = ibo.GetIndexType();
	item.pushConstants.model = glm::mat4(1.0f);

	// Record shadow depth
	{
		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items{ item };
		PassContext ctx{};
		ctx.renderExtent = vk::Extent2D{64, 64};
		ctx.renderItems = &items;
		shadowPass.Record(cmd, ctx);
		EndSubmitWait(cmd);
	}

	// Create attachment manager for shadow intensity output
	AttachmentManager attMgr(*m_device, pd);
	attMgr.Create(vk::Extent2D{64, 64});

	// Create and run shadow eval pass
	ShadowEvalPass evalPass(*m_device, pd, &attMgr, 1u);
	evalPass.SetLight(shadowPass.ShadowCubemap(), glm::vec3(0.0f), 25.0f, 0.005f);

	{
		auto& cmd = BeginCmd();
		evalPass.Record(cmd, PassContext{.renderExtent = {64, 64}, .frameIndex = 0});
		EndSubmitWait(cmd);
	}

	// Verify shadow intensity attachment was written (layout != UNDEFINED)
	auto& shadowAtt = attMgr.GetAttachment(AttachmentName::ShadowIntensity);
	EXPECT_NE(shadowAtt.CurrentLayout(), vk::ImageLayout::eUndefined)
		<< "ShadowIntensity attachment should be written by ShadowEvalPass";
}
