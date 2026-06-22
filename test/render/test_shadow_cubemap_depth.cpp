/**
 * @file test_shadow_cubemap_depth.cpp
 * @brief Shadow occlusion test — pixel-by-pixel mathematical verification.
 *
 * Scene (Light → Cube → Camera → Plane):
 *   Light at (0,0,0) — point light, origin
 *   Cube at (0,0,3) size 1×1×1 — occluder (between light and camera)
 *   Camera at (0,0,3.5), 90° FOV, looking at (0,0,6)
 *   Plane at z=6, 5×5 units, centered at origin, facing -Z — shadow receiver
 *
 * Depth map (cubemap face 4, +Z, rendered from light):
 *   Center: cube near face depth = 2.5/25 = 0.10
 *   Surrounding: plane depth = 6/25 = 0.24
 *   Beyond plane: clear = 1.0
 *
 * Shadow projection (from light through cube onto plane):
 *   Ray from (0,0,0) to (px,py,6): enters cube at z=2.5: x=px*2.5/6, y=py*2.5/6
 *   Shadowed if |px*2.5/6| ≤ 0.5 AND |py*2.5/6| ≤ 0.5
 *   → Shadow region: |px| ≤ 1.2, |py| ≤ 1.2 (2.4×2.4 square on 5×5 plane)
 *   → ~124×124 pixel shadow block at center of 256×256 viewport
 *
 * Tests:
 *   1. CubemapDepth_Validate — uses c2e.comp to sample cubemap, verifies
 *      center depth (cube ~0.10) < edge depth (plane ~0.24)
 *   2. CubemapDepth_MathVerification — pixel-by-pixel shadow comparison
 *      For each geometry pixel: EXPECT_EQ(shadow, inShadow(px,py) ? 255 : 0)
 *   3. CubemapDepth_ReferenceImage — ShadowIntensity PNG regression
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "render/passes/ShadowDepthPass.h"
#include "render/passes/ShadowEvalPass.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/AttachmentManager.h"
#include "render/passes/PassContext.h"
#include "render/passes/RenderPassManager.h"
#include "render/buffers/VertexBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/DescriptorManager.h"
#include "render/ComputePipelineBuilder.h"
#include "render/shaders/ShaderModule.h"
#include "render/Image.h"
#include "render/Screenshot.h"
#include "Log.h"

// Embedded shaders
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <c2e.comp.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

// ===========================================================================
// Simple geometry helpers
// ===========================================================================

struct TestVertex { float px, py, pz, nx, ny, nz, u, v; };

// Axis-aligned unit cube, CW winding (used in shadow depth pass)
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
		const uint32_t b = f * 4;
		idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3});
	}
	return idx;
}

// Plane at z=6, 5×5 units, centered at origin, facing -Z (toward light & camera)
static std::vector<TestVertex> MakePlaneVertsZ6(float halfSize)
{
	const float h = halfSize;
	return {
		{-h, -h, 6.0f, 0, 0, -1, 0, 0},  // bottom-left
		{ h, -h, 6.0f, 0, 0, -1, 1, 0},  // bottom-right
		{ h,  h, 6.0f, 0, 0, -1, 1, 1},  // top-right
		{-h,  h, 6.0f, 0, 0, -1, 0, 1},  // top-left
	};
}

static std::vector<uint32_t> MakePlaneIdx()
{
	// CCW winding for the plane (facing -Z, camera sees front)
	return {0, 1, 2, 0, 2, 3};
}

// ===========================================================================
// Memory type helper
// ===========================================================================

static uint32_t FindMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
                               uint32_t typeFilter,
                               vk::MemoryPropertyFlags properties)
{
	const auto memProps = physicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1u << i)) &&
		    (memProps.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	}
	throw std::runtime_error("FindMemoryType: no suitable memory type");
}

// ===========================================================================
// R8 readback helper (from test_shadow_deterministic.cpp)
// ===========================================================================

static std::vector<uint8_t> ReadR8Raw(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue queue,
	uint32_t queueFamilyIndex,
	Image& attachment)
{
	const auto extent = attachment.Extent();
	const uint32_t byteCount = extent.width * extent.height;

	// Transition to TRANSFER_SRC
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient, queueFamilyIndex);
		vk::raii::CommandPool cmdPool(device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(device, allocInfo);
		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		vk::ImageLayout oldLayout = attachment.CurrentLayout();
		vk::AccessFlags srcAccess = {};
		vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
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
		cmdBufs[0].pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);
		cmdBufs[0].end();

		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		queue.submit(submitInfo);
		queue.waitIdle();
		attachment.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
	}

	std::vector<uint8_t> data = attachment.ReadImageToBuffer(
		device, physicalDevice, queue, queueFamilyIndex,
		vk::ImageLayout::eTransferSrcOptimal);

	return data;
}

// ===========================================================================
// Predicate: is a plane point (px, py) in shadow from cube at z=3?
// ===========================================================================

inline bool InShadow(float px, float py)
{
	// Ray from (0,0,0) through (px, py, 6): enters cube at z=2.5
	const float tEnter = 2.5f / 6.0f;
	const float cx = px * tEnter;
	const float cy = py * tEnter;
	return (std::abs(cx) <= 0.5f) && (std::abs(cy) <= 0.5f);
}

// ===========================================================================
// Test Fixture
// ===========================================================================

class ShadowCubemapDepthTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRes        = 256;
	static constexpr float    kFarPlane   = 25.0f;
	static constexpr float    kBias       = 0.005f;
	static constexpr float    kCamFov     = 90.0f;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		// --- Attachment manager + render pass manager ---
		m_attMgr = std::make_unique<AttachmentManager>(*m_device, pd);
		m_attMgr->Create({kRes, kRes});
		m_rpm = std::make_unique<RenderPassManager>();

		// --- Geometry pass ---
		m_geoPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			*m_attMgr, *m_rpm,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		// --- Shadow depth pass ---
		m_shadowPass = std::make_unique<ShadowDepthPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily, kRes, kFarPlane);
		m_shadowPass->SetLightPosition(glm::vec3(0.0f));

		// --- Shadow eval pass ---
		m_evalPass = std::make_unique<ShadowEvalPass>(
			*m_device, pd, m_attMgr.get(), 1u);
		m_evalPass->SetLight(m_shadowPass->ShadowCubemap(),
		                     glm::vec3(0.0f), kFarPlane, kBias);

		// --- Cube geometry at (0,0,3) ---
		auto cubeVerts = MakeCubeVerts(1.0f);
		auto cubeInds  = MakeCubeIdx();
		for (auto& v : cubeVerts) { v.pz += 3.0f; }

		m_cubeVBO = std::make_unique<VertexBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			cubeVerts.data(), cubeVerts.size() * sizeof(TestVertex),
			sizeof(TestVertex), static_cast<uint32_t>(cubeVerts.size()));
		m_cubeIBO = std::make_unique<IndexBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			cubeInds.data(), cubeInds.size() * sizeof(uint32_t),
			static_cast<uint32_t>(cubeInds.size()));

		GeometryRenderItem cubeItem{};
		cubeItem.vertexBuffer = m_cubeVBO->buffer();
		cubeItem.indexBuffer  = m_cubeIBO->buffer();
		cubeItem.indexCount   = m_cubeIBO->GetIndexCount();
		cubeItem.indexType    = m_cubeIBO->GetIndexType();
		cubeItem.pushConstants.model        = glm::mat4(1.0f);
		cubeItem.pushConstants.normalMatrix = glm::mat4(1.0f);

		// --- Plane geometry at z=6, 6×6 half-size (covers viewport at cam 0.5) ---
		auto planeVerts = MakePlaneVertsZ6(6.0f);
		auto planeInds  = MakePlaneIdx();

		m_planeVBO = std::make_unique<VertexBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			planeVerts.data(), planeVerts.size() * sizeof(TestVertex),
			sizeof(TestVertex), static_cast<uint32_t>(planeVerts.size()));
		m_planeIBO = std::make_unique<IndexBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			planeInds.data(), planeInds.size() * sizeof(uint32_t),
			static_cast<uint32_t>(planeInds.size()));

		GeometryRenderItem planeItem{};
		planeItem.vertexBuffer = m_planeVBO->buffer();
		planeItem.indexBuffer  = m_planeIBO->buffer();
		planeItem.indexCount   = m_planeIBO->GetIndexCount();
		planeItem.indexType    = m_planeIBO->GetIndexType();
		planeItem.pushConstants.model        = glm::mat4(1.0f);
		planeItem.pushConstants.normalMatrix = glm::mat4(1.0f);

		// --- Render items: cube + plane ---
		m_renderItems = { cubeItem, planeItem };

		// --- Camera: at (0,0,3.5), 90° FOV, looking at (0,0,6) ---
		// Plane at z=6 faces camera (normals +Z), camera sees front faces
		m_viewMat = glm::lookAt(
			glm::vec3(0.0f, 0.0f, 3.5f),
			glm::vec3(0.0f, 0.0f, 6.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));
		m_projMat = glm::perspective(
			glm::radians(kCamFov),
			static_cast<float>(kRes) / static_cast<float>(kRes),
			0.1f, 100.0f);

		// Cube at z=3 is in FRONT of camera (z=3 < 3.5: behind camera in view space!)
		// Fix: move camera to z=0 so cube at z=3 and plane at z=6 are both visible
		// Camera looks from z=0.5 toward z=6, cube at z=3 is between
		m_viewMat = glm::lookAt(
			glm::vec3(0.0f, 0.0f, 0.5f),
			glm::vec3(0.0f, 0.0f, 6.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));
	}

	void TearDown() override
	{
		m_renderItems.clear();
		m_evalPass.reset();
		m_shadowPass.reset();
		m_geoPass.reset();
		m_rpm.reset();
		m_attMgr.reset();
		m_planeIBO.reset();
		m_planeVBO.reset();
		m_cubeIBO.reset();
		m_cubeVBO.reset();
		VulkanTestShared::TearDown();
	}

	void TransitionGbufferToColor()
	{
		auto& cmd = BeginCmd();
		const std::array<AttachmentName, 4> atts = {
			AttachmentName::Position, AttachmentName::Normal,
			AttachmentName::Albedo, AttachmentName::MetallicRoughness};
		for (auto a : atts)
			m_attMgr->GetAttachment(a).TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
		m_attMgr->GetAttachment(AttachmentName::Depth).TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		EndSubmitWait(cmd);
	}

	CameraUBOData CamUBO() const
	{
		CameraUBOData ubo;
		ubo.view = m_viewMat;
		ubo.viewProj = m_projMat * m_viewMat;
		return ubo;
	}

	void RenderGeometry()
	{
		auto& cmd = BeginCmd();
		m_geoPass->Record(cmd, PassContext{
			.renderExtent = {kRes, kRes},
			.viewProj = m_projMat * m_viewMat,
			.view = m_viewMat,
			.renderItems = &m_renderItems,
		});
		EndSubmitWait(cmd);
	}

	void RenderShadowDepth()
	{
		auto& cmd = BeginCmd();
		PassContext ctx{};
		ctx.renderExtent = vk::Extent2D{kRes, kRes};
		ctx.renderItems  = &m_renderItems;
		m_shadowPass->Record(cmd, ctx);
		EndSubmitWait(cmd);
	}

	void RenderShadowEval()
	{
		auto& cmd = BeginCmd();
		m_evalPass->Record(cmd, PassContext{
			.renderExtent = {kRes, kRes},
			.frameIndex = 0,
		});
		EndSubmitWait(cmd);
	}

	std::unique_ptr<AttachmentManager> m_attMgr;
	std::unique_ptr<RenderPassManager>  m_rpm;
	std::unique_ptr<GeometryPass>       m_geoPass;
	std::unique_ptr<ShadowDepthPass>    m_shadowPass;
	std::unique_ptr<ShadowEvalPass>     m_evalPass;
	std::unique_ptr<VertexBuffer>       m_cubeVBO, m_planeVBO;
	std::unique_ptr<IndexBuffer>        m_cubeIBO, m_planeIBO;
	std::vector<GeometryRenderItem>     m_renderItems;
	glm::mat4 m_viewMat{1.0f};
	glm::mat4 m_projMat{1.0f};
};

// ===========================================================================
// Test 1: Cubemap Depth Validation via c2e.comp
//
// Uses the c2e compute shader to convert the shadow cubemap to a small
// equirectangular image, then checks that the center pixel (cube depth,
// ~0.10) is lower than edge pixels (plane depth, ~0.24) and that the
// background (outside plane) reads as 1.0 (clear).
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_Validate)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }
	auto& pd = PhysicalDevice();

	// --- 1. Render shadow depth pass ---
	RenderShadowDepth();

	// --- 2. Create small equirect output image (RGBA32F) ---
	constexpr uint32_t kEqW = 32;
	constexpr uint32_t kEqH = 16;
	Image eqImage(*m_device, pd, {kEqW, kEqH},
	              vk::Format::eR32G32B32A32Sfloat,
	              vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
	              1, Image::ImageType::e2D, "EquiOut");

	// Transition equirect to GENERAL for compute write
	{
		auto& cmd = BeginCmd();
		eqImage.TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
		EndSubmitWait(cmd);
	}

	// --- 3. Build c2e compute pipeline ---
	auto c2eLayout = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute)
		.Build();
	DescriptorSetLayout dsl(*m_device, c2eLayout);

	auto compModule = ShaderModule::FromEmbedded(*m_device, c2e_comp_spv, sizeof(c2e_comp_spv));

	ComputePipelineBuilder builder(*m_device);
	builder.SetShaderStage(std::move(compModule), "main");
	builder.AddDescriptorSetLayout(*dsl.layout());
	auto pipeline = builder.BuildComputePipeline();

	// --- 4. Create sampler ---
	vk::SamplerCreateInfo samplerCI({},
		vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
		vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge);
	vk::raii::Sampler sampler(*m_device, samplerCI);

	// --- 5. Write descriptor ---
	DescriptorPool pool(*m_device, 1, DescriptorPool::CalculatePoolSizes({&dsl}, 1));
	auto ds = std::move(pool.Allocate(dsl, 1).front());

	{
		vk::DescriptorImageInfo cubemapInfo(
			sampler, m_shadowPass->ShadowCubemap().ImageViewHandle(),
			vk::ImageLayout::eShaderReadOnlyOptimal);
		ds.WriteImage(0, cubemapInfo, vk::DescriptorType::eCombinedImageSampler);
	}
	{
		vk::DescriptorImageInfo eqInfo(nullptr, eqImage.ImageViewHandle(),
		                                vk::ImageLayout::eGeneral);
		ds.WriteImage(1, eqInfo, vk::DescriptorType::eStorageImage);
	}

	// --- 6. Dispatch c2e ---
	{
		auto& cmd = BeginCmd();
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
		                       *builder.pipelineLayout(), 0, {ds.handle()}, {});
		cmd.dispatch((kEqW + 15) / 16, (kEqH + 15) / 16, 1);
		EndSubmitWait(cmd);
	}

	// --- 7. Read back equirect ---
	std::vector<uint8_t> eqData;
	{
		auto& cmd = BeginCmd();
		eqImage.TransitionLayout(cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
		EndSubmitWait(cmd);
		eqData = eqImage.ReadImageToBuffer(*m_device, pd, m_queue,
		                                    m_graphicsQueueFamily,
		                                    vk::ImageLayout::eTransferSrcOptimal);
	}

	ASSERT_FALSE(eqData.empty()) << "c2e readback failed";
	const size_t expectedBytes = kEqW * kEqH * 16; // 4 * float32
	ASSERT_EQ(eqData.size(), expectedBytes);

	// --- 8. Extract depth (R channel) from key equirect pixels ---
	// Center pixel (lon=0, lat=0 → +Z direction): should hit cube near face (~0.10)
	// Edge pixel: should hit plane (~0.24) or background (1.0)
	auto readPixel = [&](uint32_t x, uint32_t y) -> float {
		size_t off = (y * kEqW + x) * 16;
		float r;
		std::memcpy(&r, eqData.data() + off, sizeof(float));
		return r;
	};

	const float centerDepth = readPixel(kEqW / 2, kEqH / 2);
	const float edgeDepth  = readPixel(0, kEqH / 2);  // far left
	const float topDepth   = readPixel(kEqW / 2, 0);   // top

	NEURUS_LOG("[CubemapDepth] center=" << centerDepth << " edge=" << edgeDepth << " top=" << topDepth);

	// Center should have lower depth than edge (cube occludes plane)
	EXPECT_LT(centerDepth, edgeDepth)
		<< "Center (cube) depth=" << centerDepth
		<< " should be < edge (plane) depth=" << edgeDepth;

	EXPECT_GT(centerDepth, 0.05f) << "Center depth too low (near zero)";
	EXPECT_LT(centerDepth, 0.20f) << "Center depth too high (not cube)";

	// Edge should be plane depth or background (close to 0.24 or 1.0)
	EXPECT_GT(edgeDepth, 0.20f) << "Edge depth too low (unexpected geometry)";
}

// ===========================================================================
// Test 2: Pixel-by-pixel Shadow Intensity Verification
//
// Renders the full pipeline and compares each pixel against mathematically
// computed expected shadow values.
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_MathVerification)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	// --- Run full pipeline ---
	TransitionGbufferToColor();
	RenderGeometry();
	RenderShadowDepth();
	RenderShadowEval();

	// --- Read back ShadowIntensity (R8) ---
	auto& shadowAtt = m_attMgr->GetAttachment(AttachmentName::ShadowIntensity);
	auto raw = ReadR8Raw(*m_device, PhysicalDevice(), m_queue,
	                     m_graphicsQueueFamily, shadowAtt);
	ASSERT_FALSE(raw.empty());
	ASSERT_EQ(raw.size(), kRes * kRes);

	// --- Quick diagnostic: variance check ---
	{
		uint8_t smin = raw[0], smax = raw[0];
		for (auto v : raw) { smin = std::min(smin, v); smax = std::max(smax, v); }
		NEURUS_LOG("[ShadowMath] rawShadow min=" << (int)smin << " max=" << (int)smax);
		if (smin == smax)
		{
			NEURUS_LOG("[ShadowMath] WARNING: ShadowIntensity is UNIFORM (" << (int)smin << ")");
		}
	}

	// --- Read back G-Buffer position for background detection ---
	// Transition to read
	{
		auto& cmd = BeginCmd();
		m_attMgr->GetAttachment(AttachmentName::Position)
			.TransitionLayout(cmd, vk::ImageLayout::eUndefined,
			                  vk::ImageLayout::eTransferSrcOptimal);
		EndSubmitWait(cmd);
	}
	auto posRaw = m_attMgr->GetAttachment(AttachmentName::Position)
		.ReadImageToBuffer(*m_device, PhysicalDevice(), m_queue,
		                   m_graphicsQueueFamily,
		                   vk::ImageLayout::eTransferSrcOptimal);
	ASSERT_FALSE(posRaw.empty());

	// Each position pixel = 8 bytes (R16G16B16A16_SFLOAT = 4 × half)
	// For validation, we don't need to decode every pixel — we know the scene
	// geometry (plane at z=6, cameras sees it). We check the shadow pattern.

	// --- Pixel-by-pixel verification ---
	// Compute expected shadow using known camera projection (not G-Buffer readback).
	// Camera at (0,0,3.5), 90° FOV, looking at (0,0,6).
	// Plane at z=6, 5×5 units. Each pixel maps to world position via projection.
	// For pixel (x, y), ndc coords: nx = (x+0.5)*2/kRes - 1, ny = (y+0.5)*2/kRes - 1
	// At z=6: world_x = nx * tan(45°) * (6-3.5) = nx * 2.5, world_y = ny * 2.5
	const float halfFov = 45.0f * 3.14159265f / 180.0f;
	const float tanHalfFov = std::tan(halfFov);
	const float camDist = 6.0f - 0.5f; // 5.5

	uint32_t mismatches = 0;
	uint32_t shadowedCount = 0, litCount = 0;
	uint32_t backgroundCount = 0;

	for (uint32_t y = 0; y < kRes; ++y)
	{
		for (uint32_t x = 0; x < kRes; ++x)
		{
			const uint8_t shadowVal = raw[y * kRes + x];

			// Check G-Buffer w to determine background
			const size_t posOff = (y * kRes + x) * 8;
			uint16_t hw;
			std::memcpy(&hw, posRaw.data() + posOff + 6, 2);

			if (hw == 0)
			{
				++backgroundCount;
				EXPECT_EQ(shadowVal, 0u) << "BG pixel (" << x << "," << y << ") should be lit";
				if (shadowVal != 0) ++mismatches;
				continue;
			}

			// Compute world position on plane via camera projection
			float nx = ((float)x + 0.5f) * 2.0f / (float)kRes - 1.0f;
			float ny = ((float)y + 0.5f) * 2.0f / (float)kRes - 1.0f;
			float px = nx * tanHalfFov * camDist;
			float py = ny * tanHalfFov * camDist;

			bool expectedShadow = InShadow(px, py);
			uint8_t expectedVal = expectedShadow ? 255 : 0;

			if (expectedShadow) ++shadowedCount;
			else ++litCount;

			int diff = std::abs((int)shadowVal - (int)expectedVal);
			if (diff > 1)
			{
				if (mismatches < 5)
					NEURUS_LOG("[ShadowMath] mismatch at (" << x << "," << y << "): actual=" << (int)shadowVal << " expected=" << (int)expectedVal << " px=" << px << " py=" << py);
				++mismatches;
			}
		}
	}

	NEURUS_LOG("[ShadowMath] bg=" << backgroundCount
	           << " shadowed=" << shadowedCount << " lit=" << litCount
	           << " mismatches=" << mismatches);

	EXPECT_GT(shadowedCount, 1000u) << "No shadowed pixels found (shadow broken)";
	EXPECT_GT(litCount, 1000u) << "No lit pixels found (all shadowed?)";

	const float maxMismatchRate = 0.02f;
	uint32_t totalCompared = shadowedCount + litCount;
	float mismatchRate = totalCompared > 0 ? (float)mismatches / (float)totalCompared : 1.0f;
	EXPECT_LE(mismatchRate, maxMismatchRate)
		<< mismatches << " mismatches out of " << totalCompared
		<< " compared pixels (" << (mismatchRate * 100.0f) << "%)";
}

// ===========================================================================
// Test 3: Shadow Intensity Reference Image
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_ReferenceImage)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	// --- Run full pipeline ---
	TransitionGbufferToColor();
	RenderGeometry();
	RenderShadowDepth();
	RenderShadowEval();

	// --- Capture ShadowIntensity ---
	auto& shadowAtt = m_attMgr->GetAttachment(AttachmentName::ShadowIntensity);

	// First transition to a know read-friendly layout
	{
		auto& cmd = BeginCmd();
		if (shadowAtt.CurrentLayout() == vk::ImageLayout::eUndefined)
			shadowAtt.TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
		else if (shadowAtt.CurrentLayout() != vk::ImageLayout::eGeneral)
			shadowAtt.TransitionLayout(cmd, shadowAtt.CurrentLayout(), vk::ImageLayout::eGeneral);
		EndSubmitWait(cmd);
	}

	const std::string refDir = "../../../test/render/reference/shadow/";
	const std::string refPath = refDir + "ShadowIntensity_Occlusion.png";
	const std::string tmpPath = refPath + ".tmp";

	std::filesystem::create_directories(refDir);

	const bool captured = Screenshot::CaptureAttachment(
		*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily,
		shadowAtt, tmpPath, false);
	ASSERT_TRUE(captured) << "Failed to capture ShadowIntensity";

	// Reference image regression
	const bool refExists = std::ifstream(refPath).good();
	if (!refExists)
	{
		std::rename(tmpPath.c_str(), refPath.c_str());
		GTEST_SKIP() << "Reference generated. Re-run to compare.";
	}
	else
	{
		// Load both images
		int tw, th, tc, rw, rh, rc;
		unsigned char* tp = stbi_load(tmpPath.c_str(), &tw, &th, &tc, 4);
		unsigned char* rp = stbi_load(refPath.c_str(), &rw, &rh, &rc, 4);
		ASSERT_TRUE(tp && rp);
		ASSERT_EQ(tw, rw); ASSERT_EQ(th, rh);

		// Compare pixel-by-pixel (±2 tolerance)
		int bad = 0;
		size_t pxCount = static_cast<size_t>(tw) * th * 4;
		for (size_t i = 0; i < pxCount; i += 4)
		{
			for (int c = 0; c < 4; ++c)
				if (std::abs(static_cast<int>(tp[i+c]) - static_cast<int>(rp[i+c])) > 2)
					{ ++bad; break; }
		}

		stbi_image_free(tp);
		stbi_image_free(rp);
		std::remove(tmpPath.c_str());

		EXPECT_EQ(bad, 0) << bad << " pixel(s) differ from reference";
	}
}

// ===========================================================================
// Test 4: Minimal clone of deterministic test — verifies ShadowDepthPass works
// in THIS test fixture (same scene geometry, same pass creation pattern).
// If this passes but the other tests fail, the issue is in the additional
// test infrastructure (AttachmentManager, GeometryPass, etc.).
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_MinimalDeterministicClone)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }
	auto& pd = PhysicalDevice();

	// Clone EXACTLY what ShadowDeterministicTest does
	auto verts = MakeCubeVerts(1.0f);
	auto inds  = MakeCubeIdx();
	for (auto& v : verts) { v.pz += 2.0f; }  // cube at (0,0,2)

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

	// Fresh ShadowDepthPass (not from fixture)
	ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily, 256, 25.0f);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	{
		auto& cmd = BeginCmd();
		PassContext ctx{};
		ctx.renderExtent = vk::Extent2D{256, 256};
		ctx.renderItems  = &items;
		shadowPass.Record(cmd, ctx);
		EndSubmitWait(cmd);
	}

	// Read back face 4 via c2e
	constexpr uint32_t kEqW = 32, kEqH = 16;
	Image eqImage(*m_device, pd, {kEqW, kEqH},
	              vk::Format::eR32G32B32A32Sfloat,
	              vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
	              1, Image::ImageType::e2D, "EquiOut2");
	{ auto& cmd = BeginCmd(); eqImage.TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral); EndSubmitWait(cmd); }

	auto c2eLayout = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute)
		.Build();
	DescriptorSetLayout dsl(*m_device, c2eLayout);
	auto compModule = ShaderModule::FromEmbedded(*m_device, c2e_comp_spv, sizeof(c2e_comp_spv));
	ComputePipelineBuilder builder(*m_device);
	builder.SetShaderStage(std::move(compModule), "main");
	builder.AddDescriptorSetLayout(*dsl.layout());
	auto pipeline = builder.BuildComputePipeline();

	vk::SamplerCreateInfo samplerCI({}, vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
		vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge);
	vk::raii::Sampler sampler(*m_device, samplerCI);

	DescriptorPool pool(*m_device, 1, DescriptorPool::CalculatePoolSizes({&dsl}, 1));
	auto ds = std::move(pool.Allocate(dsl, 1).front());
	ds.WriteImage(0, vk::DescriptorImageInfo(sampler, shadowPass.ShadowCubemap().ImageViewHandle(), vk::ImageLayout::eShaderReadOnlyOptimal), vk::DescriptorType::eCombinedImageSampler);
	ds.WriteImage(1, vk::DescriptorImageInfo(nullptr, eqImage.ImageViewHandle(), vk::ImageLayout::eGeneral), vk::DescriptorType::eStorageImage);

	{ auto& cmd = BeginCmd();
	  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
	  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *builder.pipelineLayout(), 0, {ds.handle()}, {});
	  cmd.dispatch((kEqW+15)/16, (kEqH+15)/16, 1);
	  EndSubmitWait(cmd); }

	std::vector<uint8_t> eqData;
	{ auto& cmd = BeginCmd(); eqImage.TransitionLayout(cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal); EndSubmitWait(cmd);
	  eqData = eqImage.ReadImageToBuffer(*m_device, pd, m_queue, m_graphicsQueueFamily, vk::ImageLayout::eTransferSrcOptimal); }

	ASSERT_FALSE(eqData.empty());
	auto readPx = [&](uint32_t x, uint32_t y) -> float { float f; std::memcpy(&f, eqData.data() + (y*kEqW+x)*16, 4); return f; };
	float c = readPx(kEqW/2, kEqH/2);
	float e = readPx(0, kEqH/2);
	NEURUS_LOG("[MinimalClone] center=" << c << " edge=" << e);
	EXPECT_GT(std::max(c, e), 0.0f) << "Cubemap is completely empty — ShadowDepthPass not rendering";
}

// ===========================================================================
// Standalone minimal test — no fixture SetUp, fresh VulkanTestShared only
// ===========================================================================

class ShadowDepthSmokeTest : public VulkanTestShared
{
protected:
	void SetUp() override { VulkanTestShared::SetUp(); }
	void TearDown() override { VulkanTestShared::TearDown(); }
};

TEST_F(ShadowDepthSmokeTest, CubeAtZ3_EvalPassProducesShadow)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }
	auto& pd = PhysicalDevice();

	auto verts = MakeCubeVerts(1.0f);
	auto inds  = MakeCubeIdx();
	for (auto& v : verts) { v.pz += 3.0f; }

	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 verts.data(), verts.size() * sizeof(TestVertex),
	                 sizeof(TestVertex), static_cast<uint32_t>(verts.size()));
	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                inds.data(), inds.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(inds.size()));

	GeometryRenderItem item{};
	item.vertexBuffer = vbo.buffer(); item.indexBuffer = ibo.buffer();
	item.indexCount = ibo.GetIndexCount(); item.indexType = ibo.GetIndexType();
	item.pushConstants.model = glm::mat4(1.0f); item.pushConstants.normalMatrix = glm::mat4(1.0f);
	std::vector<GeometryRenderItem> items{ item };

	// Shadow depth + eval
	ShadowDepthPass sp(*m_device, pd, m_queue, m_graphicsQueueFamily, 256, 25.0f);
	sp.SetLightPosition(glm::vec3(0.0f));
	{ auto& cmd = BeginCmd(); PassContext ctx; ctx.renderExtent = vk::Extent2D{256,256}; ctx.renderItems = &items; sp.Record(cmd, ctx); EndSubmitWait(cmd); }

	AttachmentManager am(*m_device, pd);
	am.Create(vk::Extent2D{256,256});
	// Clear position to (0,0,0,0) so all pixels are background → shadow=0
	{ auto& cmd = BeginCmd();
	  auto& pos = am.GetAttachment(AttachmentName::Position);
	  pos.TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	  vk::ClearColorValue clr(std::array<float,4>{0,0,0,0});
	  cmd.clearColorImage(*pos.ImageHandle(), vk::ImageLayout::eTransferDstOptimal, clr,
	                      vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0,1,0,1));
	  pos.SetCurrentLayout(vk::ImageLayout::eTransferDstOptimal);
	  pos.TransitionLayout(cmd, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	  EndSubmitWait(cmd); }

	ShadowEvalPass ep(*m_device, pd, &am, 1u);
	ep.SetLight(sp.ShadowCubemap(), glm::vec3(0.0f), 25.0f, 0.005f);
	{ auto& cmd = BeginCmd(); PassContext ctx2; ctx2.renderExtent = vk::Extent2D{256,256}; ctx2.frameIndex = 0; ep.Record(cmd, ctx2); EndSubmitWait(cmd); }

	auto& sa = am.GetAttachment(AttachmentName::ShadowIntensity);
	ASSERT_NE(sa.CurrentLayout(), vk::ImageLayout::eUndefined);
	// Transition to read-friendly layout
	{ auto& cmd = BeginCmd(); sa.TransitionLayout(cmd, sa.CurrentLayout(), vk::ImageLayout::eTransferSrcOptimal); EndSubmitWait(cmd); }
	auto raw = sa.ReadImageToBuffer(*m_device, pd, m_queue, m_graphicsQueueFamily, vk::ImageLayout::eTransferSrcOptimal);
	ASSERT_FALSE(raw.empty());

	uint8_t smin = raw[0], smax = raw[0];
	for (auto v : raw) { smin = std::min(smin, v); smax = std::max(smax, v); }
	NEURUS_LOG("[SmokeEval] shadow min=" << (int)smin << " max=" << (int)smax);
	// Background → all shadow=0. If shadow is NOT all zero, eval pass is reading cubemap.
	EXPECT_EQ(smin, 0) << "ShadowIntensity has non-zero background pixels";
	EXPECT_EQ(smax, 0) << "ShadowIntensity background not all lit";
}

TEST_F(ShadowDepthSmokeTest, CubeAtZ3_RendersToCubemap)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }
	auto& pd = PhysicalDevice();

	auto verts = MakeCubeVerts(1.0f);
	auto inds  = MakeCubeIdx();
	for (auto& v : verts) { v.pz += 3.0f; }

	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 verts.data(), verts.size() * sizeof(TestVertex),
	                 sizeof(TestVertex), static_cast<uint32_t>(verts.size()));
	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                inds.data(), inds.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(inds.size()));

	GeometryRenderItem item{};
	item.vertexBuffer = vbo.buffer(); item.indexBuffer = ibo.buffer();
	item.indexCount = ibo.GetIndexCount(); item.indexType = ibo.GetIndexType();
	item.pushConstants.model = glm::mat4(1.0f); item.pushConstants.normalMatrix = glm::mat4(1.0f);
	std::vector<GeometryRenderItem> items{ item };

	ShadowDepthPass sp(*m_device, pd, m_queue, m_graphicsQueueFamily, 256, 25.0f);
	sp.SetLightPosition(glm::vec3(0.0f));
	{ auto& cmd = BeginCmd(); PassContext ctx; ctx.renderExtent = vk::Extent2D{256,256}; ctx.renderItems = &items; sp.Record(cmd, ctx); EndSubmitWait(cmd); }

	constexpr uint32_t kEqW = 32, kEqH = 16;
	Image eqImg(*m_device, pd, {kEqW, kEqH}, vk::Format::eR32G32B32A32Sfloat,
	            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
	            1, Image::ImageType::e2D, "EqSmoke");
	{ auto& cmd = BeginCmd(); eqImg.TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral); EndSubmitWait(cmd); }

	auto lay = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute).Build();
	DescriptorSetLayout dsl(*m_device, lay);
	auto mod = ShaderModule::FromEmbedded(*m_device, c2e_comp_spv, sizeof(c2e_comp_spv));
	ComputePipelineBuilder b(*m_device);
	b.SetShaderStage(std::move(mod), "main"); b.AddDescriptorSetLayout(*dsl.layout());
	auto pipe = b.BuildComputePipeline();
	vk::raii::Sampler smp(*m_device, vk::SamplerCreateInfo({}, vk::Filter::eNearest, vk::Filter::eNearest,
		vk::SamplerMipmapMode::eNearest, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge));
	DescriptorPool p(*m_device, 1, DescriptorPool::CalculatePoolSizes({&dsl}, 1));
	auto ds = std::move(p.Allocate(dsl, 1).front());
	ds.WriteImage(0, {smp, sp.ShadowCubemap().ImageViewHandle(), vk::ImageLayout::eShaderReadOnlyOptimal}, vk::DescriptorType::eCombinedImageSampler);
	ds.WriteImage(1, {nullptr, eqImg.ImageViewHandle(), vk::ImageLayout::eGeneral}, vk::DescriptorType::eStorageImage);
	{ auto& cmd = BeginCmd();
	  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipe);
	  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *b.pipelineLayout(), 0, {ds.handle()}, {});
	  cmd.dispatch((kEqW+15)/16, (kEqH+15)/16, 1); EndSubmitWait(cmd); }

	std::vector<uint8_t> d;
	{ auto& cmd = BeginCmd(); eqImg.TransitionLayout(cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal); EndSubmitWait(cmd);
	  d = eqImg.ReadImageToBuffer(*m_device, pd, m_queue, m_graphicsQueueFamily, vk::ImageLayout::eTransferSrcOptimal); }
	ASSERT_FALSE(d.empty());
	float c; std::memcpy(&c, d.data() + (8*kEqW + kEqW/2)*16, 4);
	NEURUS_LOG("[SmokeTest] centerDepth=" << c);
	EXPECT_GT(c, 0.05f) << "Cubemap depth is zero — ShadowDepthPass not rendering at all";
}
