/**
 * @file test_shadow_deterministic.cpp
 * @brief Deterministic tests for point light shadow mapping.
 *
 * Tests:
 *   1. DepthCubemap_HitsExpectedDepth — Full end-to-end pipeline (
 *      GeometryPass → ShadowDepthPass → ShadowEvalPass) and verifies
 *      that ShadowIntensity shows expected self-shadow pattern.
 *   2. FaceViewProj_PlusZ_LooksForward — Verifies +Z face view-projection.
 *   3. ShadowEval_BackgroundProducesZero — Background pixels = 0.0 (lit).
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "render/passes/ShadowDepthPass.h"
#include "render/passes/ShadowEvalPass.h"
#include "render/passes/AttachmentManager.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/PassContext.h"
#include "render/passes/RenderPassManager.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"
#include "render/Image.h"
#include "Log.h"

// Embedded G-Buffer shaders
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Simple cube geometry (axis-aligned unit cube, CW winding)
// ---------------------------------------------------------------------------

struct TestVertex { float px, py, pz, nx, ny, nz, u, v; };

static std::vector<TestVertex> MakeCubeVerts(float size)
{
	const float h = size * 0.5f;
	std::vector<TestVertex> v;
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
// Helper: read back an R8_UNORM attachment
// ---------------------------------------------------------------------------

struct ReadR8Result
{
	std::vector<uint8_t> data;
	uint8_t minVal = 0;
	uint8_t maxVal = 0;
	bool allSame = true;
};

static ReadR8Result ReadR8Attachment(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue queue,
	uint32_t queueFamilyIndex,
	Image& attachment)
{
	ReadR8Result result{};

	// Transition to TRANSFER_SRC
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);

		cmdBufs[0].begin(vk::CommandBufferBeginInfo(
			vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		vk::AccessFlags srcAccess = {};
		vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		vk::ImageLayout oldLayout = attachment.CurrentLayout();
		if (oldLayout != vk::ImageLayout::eUndefined)
		{
			srcAccess = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
			srcStage = vk::PipelineStageFlagBits::eAllCommands;
		}

		vk::ImageMemoryBarrier barrier(
			srcAccess, vk::AccessFlagBits::eTransferRead,
			oldLayout, vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*attachment.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBufs[0].pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eTransfer,
		                           {}, {}, {}, barrier);
		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();

		attachment.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
	}

	result.data = attachment.ReadImageToBuffer(
		device, physicalDevice, queue, queueFamilyIndex,
		vk::ImageLayout::eTransferSrcOptimal);

	if (!result.data.empty())
	{
		result.minVal = result.data[0];
		result.maxVal = result.data[0];
		result.allSame = true;
		for (size_t i = 1; i < result.data.size(); ++i)
		{
			result.minVal = std::min(result.minVal, result.data[i]);
			result.maxVal = std::max(result.maxVal, result.data[i]);
			if (result.data[i] != result.data[0])
				result.allSame = false;
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ShadowDeterministicTest : public VulkanTestShared
{
protected:
	void SetUp() override { VulkanTestShared::SetUp(); }
	void TearDown() override { VulkanTestShared::TearDown(); }
};

// ===========================================================================
// Test 1: End-to-end deterministic test
//
// Scene:
//   Cube at (0, 0, 2), size 1x1x1 (front -Z face at z=1.5)
//   Light at (0, 0, 0)
//   Camera at (0, 0, 5) looking at (0, 0, 2)
//
// The camera sees the +Z face (z~2.5). The light is behind the cube.
// The cube's -Z face at z=1.5 faces the light and thus occludes the
// +Z face. So the camera sees the cube as self-shadowed.
//
// Verifies: ShadowIntensity shows variation (cube self-shadowed,
//           background lit).
// ===========================================================================

TEST_F(ShadowDeterministicTest, DepthCubemap_HitsExpectedDepth)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();

	constexpr uint32_t kWidth     = 256;
	constexpr uint32_t kHeight    = 256;
	constexpr uint32_t kShadowRes = 1024;
	constexpr float    kFarPlane  = 25.0f;

	// --- 1. Cube geometry at (0, 0, 2) ---
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
	item.pushConstants.model        = glm::mat4(1.0f);
	item.pushConstants.normalMatrix = glm::mat4(1.0f);

	std::vector<GeometryRenderItem> items{ item };

	// --- 2. Pipelines ---
	AttachmentManager attMgr(*m_device, pd);
	attMgr.Create(vk::Extent2D{kWidth, kHeight});

	RenderPassManager rpm;

	GeometryPass geoPass(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                     attMgr, rpm,
	                     gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
	                     gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

	ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                           kShadowRes, kFarPlane);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	ShadowEvalPass evalPass(*m_device, pd, &attMgr, 1u);
	evalPass.SetLight(shadowPass.ShadowCubemap(), glm::vec3(0.0f), kFarPlane, 0.005f);

	// --- 3. Camera: at (0, 0, 5), looking at (0, 0, 2) ---
	const glm::mat4 viewMat = glm::lookAt(
		glm::vec3(0, 0, 5), glm::vec3(0, 0, 2), glm::vec3(0, 1, 0));
	const glm::mat4 projMat = glm::perspective(
		glm::radians(60.0f),
		static_cast<float>(kWidth) / static_cast<float>(kHeight),
		0.1f, 100.0f);

	CameraUBOData camUBO{};
	camUBO.view     = viewMat;
	camUBO.viewProj = projMat * viewMat;

	// --- 4. Transition G-Buffer to color attachment ---
	{
		auto& cmd = BeginCmd();
		const std::array<AttachmentName, 4> colorAtts = {
			AttachmentName::Position, AttachmentName::Normal,
			AttachmentName::Albedo, AttachmentName::MetallicRoughness,
		};
		for (const auto& att : colorAtts)
		{
			attMgr.GetAttachment(att).TransitionLayout(
				cmd, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal);
		}
		attMgr.GetAttachment(AttachmentName::Depth).TransitionLayout(
			cmd, vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);
		EndSubmitWait(cmd);
	}

	// --- 5. Geometry pass ---
	{
		auto& cmd = BeginCmd();
		geoPass.Record(cmd, PassContext{
			.renderExtent = {kWidth, kHeight},
			.viewProj     = camUBO.viewProj,
			.view         = camUBO.view,
			.renderItems  = &items,
		});
		EndSubmitWait(cmd);
	}

	// --- 6. Shadow depth pass ---
	{
		auto& cmd = BeginCmd();
		PassContext shadowCtx{};
		shadowCtx.renderExtent = vk::Extent2D{kShadowRes, kShadowRes};
		shadowCtx.renderItems  = &items;
		shadowPass.Record(cmd, shadowCtx);
		EndSubmitWait(cmd);
	}

	// --- 7. Shadow eval pass ---
	{
		auto& cmd = BeginCmd();
		evalPass.Record(cmd, PassContext{
			.renderExtent = {kWidth, kHeight},
			.frameIndex   = 0,
		});
		EndSubmitWait(cmd);
	}

	// --- 8. Read back ShadowIntensity ---
	auto& shadowAtt = attMgr.GetAttachment(AttachmentName::ShadowIntensity);
	auto result = ReadR8Attachment(*m_device, pd, m_queue, m_graphicsQueueFamily, shadowAtt);

	ASSERT_FALSE(result.data.empty()) << "Failed to read back ShadowIntensity";
	ASSERT_EQ(result.data.size(), kWidth * kHeight);

	// --- 9. Verify ---
	EXPECT_FALSE(result.allSame)
		<< "ShadowIntensity uniform (" << static_cast<int>(result.minVal)
		<< "). Shadow pass may not be producing results.";

	// Count shadowed (>= 128 = >= 0.5) vs lit (< 128)
	uint32_t shadowed = 0, lit = 0;
	for (auto v : result.data)
	{
		if (v >= 128) ++shadowed;
		else          ++lit;
	}

	EXPECT_GT(shadowed, 0u) << "No shadowed pixels — self-shadow broken";
	EXPECT_GT(lit, 0u)      << "No lit pixels — background should be lit";

	const float frac = static_cast<float>(shadowed) / (kWidth * kHeight);
	EXPECT_GT(frac, 0.02f) << "Shadow fraction too low (" << frac << ")";
	EXPECT_LT(frac, 0.70f) << "Shadow fraction too high (" << frac << ")";

	NEURUS_LOG("[ShadowDeterministic] shadowed=" << shadowed << " lit=" << lit
	           << " frac=" << frac);
}

// ===========================================================================
// Test 2: Face view-proj math
// ===========================================================================

TEST_F(ShadowDeterministicTest, FaceViewProj_PlusZ_LooksForward)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 25.0f);
	const glm::mat4 viewPZ = glm::lookAt(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0));

	const glm::vec4 clip = proj * viewPZ * glm::vec4(0, 0, 1.5f, 1.0f);
	const float ndcZ = clip.z / clip.w;
	EXPECT_GT(ndcZ, 0.0f) << "z=1.5 should be in front of near";
	EXPECT_LT(ndcZ, 1.0f) << "z=1.5 should be before far";

	const glm::vec4 clipNear = proj * viewPZ * glm::vec4(0, 0, 0.05f, 1.0f);
	EXPECT_LT(clipNear.z / clipNear.w, 0.0f) << "z=0.05 behind near";

	const glm::vec4 clipFar = proj * viewPZ * glm::vec4(0, 0, 30.0f, 1.0f);
	EXPECT_GT(clipFar.z / clipFar.w, 1.0f) << "z=30 beyond far";
}

// ===========================================================================
// Test 3: Background pixels are fully lit
// ===========================================================================

TEST_F(ShadowDeterministicTest, ShadowEval_BackgroundProducesZero)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();
	constexpr uint32_t kRes = 64;
	constexpr float kFar = 25.0f;

	// Empty shadow cubemap
	ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily, 16, kFar);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	{
		auto& cmd = BeginCmd();
		PassContext ctx{};
		ctx.renderExtent = vk::Extent2D{16, 16};
		ctx.renderItems  = nullptr;
		shadowPass.Record(cmd, ctx);
		EndSubmitWait(cmd);
	}

	// Attachment manager + clear Position to (0,0,0,0)
	AttachmentManager attMgr(*m_device, pd);
	attMgr.Create(vk::Extent2D{kRes, kRes});

	{
		auto& cmd = BeginCmd();
		auto& posAtt = attMgr.GetAttachment(AttachmentName::Position);

		vk::ImageMemoryBarrier barrier(
			{}, vk::AccessFlagBits::eTransferWrite,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*posAtt.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
		                    vk::PipelineStageFlagBits::eTransfer,
		                    {}, {}, {}, barrier);

		vk::ClearColorValue clearBlack(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
		cmd.clearColorImage(*posAtt.ImageHandle(),
		                    vk::ImageLayout::eTransferDstOptimal,
		                    clearBlack,
		                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
		                                              0, 1, 0, 1));
		posAtt.SetCurrentLayout(vk::ImageLayout::eTransferDstOptimal);

		posAtt.TransitionLayout(cmd,
		                        vk::ImageLayout::eTransferDstOptimal,
		                        vk::ImageLayout::eShaderReadOnlyOptimal);
		EndSubmitWait(cmd);
	}

	// Shadow eval
	ShadowEvalPass evalPass(*m_device, pd, &attMgr, 1u);
	evalPass.SetLight(shadowPass.ShadowCubemap(), glm::vec3(0.0f), kFar, 0.005f);

	{
		auto& cmd = BeginCmd();
		evalPass.Record(cmd, PassContext{
			.renderExtent = {kRes, kRes},
			.frameIndex   = 0,
		});
		EndSubmitWait(cmd);
	}

	auto& shadowAtt = attMgr.GetAttachment(AttachmentName::ShadowIntensity);
	auto result = ReadR8Attachment(*m_device, pd, m_queue, m_graphicsQueueFamily, shadowAtt);
	ASSERT_FALSE(result.data.empty());

	EXPECT_EQ(result.minVal, 0u) << "All background = shadow 0 (lit)";
	EXPECT_EQ(result.maxVal, 0u) << "All background = shadow 0 (lit)";
	EXPECT_NE(shadowAtt.CurrentLayout(), vk::ImageLayout::eUndefined);
}
