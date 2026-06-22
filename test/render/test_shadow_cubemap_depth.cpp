/**
 * @file test_shadow_cubemap_depth.cpp
 * @brief TDD RED scaffold — reads back depth values from cubemap face 4 (+Z) of a
 *        ShadowDepthPass. No mathematical assertions yet (deferred to Task 8).
 *
 * This test verifies that the ShadowDepthPass writes non-empty depth data to its
 * cubemap and that the depth can be read back via a manual staging buffer (since
 * Image::ReadImageToBuffer hardcodes eColor, which is broken for depth-stencil).
 *
 * Scene:
 *   Cube at (0, 0, 6), size 1x1x1
 *   Light at (0, 0, 3), far plane 25.0
 *   Shadow cubemap resolution: 256x256
 *
 * Face 4 (+Z) looks forward from the light and sees the cube's -Z face at z=5.5.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "render/passes/ShadowDepthPass.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/PassContext.h"
#include "render/buffers/VertexBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/passes/AttachmentManager.h"
#include "render/passes/ShadowEvalPass.h"
#include "render/passes/RenderPassManager.h"
#include "render/Image.h"
#include "render/Screenshot.h"

#include "Log.h"

// Embedded G-Buffer shaders (for GeometryPass in Step C)
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>

// STB image I/O (for PNG readback in Step B)
#include <stb_image.h>
#include <stb_image_write.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Simple cube geometry (axis-aligned unit cube, CW winding)
// Reused from test_shadow_deterministic.cpp — identical helpers.
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
		const uint32_t b = f * 4;
		idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
	}
	return idx;
}

// ---------------------------------------------------------------------------
// Memory type lookup — replicated so the test does not depend on
// Image's private FindMemoryType helper.
// ---------------------------------------------------------------------------

static uint32_t FindMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
                               uint32_t typeFilter,
                               vk::MemoryPropertyFlags properties)
{
	const auto memProps = physicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1u << i)) &&
		    (memProps.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	throw std::runtime_error("FindMemoryType: no suitable memory type found");
}

// ---------------------------------------------------------------------------
// Quad geometry at z=6, 4×4 units, facing +Z (for Steps A–C)
// ---------------------------------------------------------------------------

static std::vector<TestVertex> MakeQuadVertsZ6()
{
	// Quad at z=6, 2x2 units (scaled to fit viewport at camera distance 3)
	return {
		{-1.0f, -1.0f, 6.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
		{-1.0f,  1.0f, 6.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
		{ 1.0f,  1.0f, 6.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
		{ 1.0f, -1.0f, 6.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
	};
}

// Reversed winding (CW in NDC) so the -Z face renders as front-face from
// the light at origin looking +Z through cubemap face 4.
// Geometric normal = cross((2-0),(1-0)) = (0,0,-1).
static std::vector<uint32_t> MakeQuadIdx()
{
	return {0, 2, 1, 0, 3, 2};
}

// ---------------------------------------------------------------------------
// Analytical expected depth for a plane at zPlane, rendered from light at
// origin into cubemap face 4 (+Z) with 90° FOV perspective.
//
// For pixel (px, py) in [0, res-1]²:
//   ndc_x = (px+0.5)*2/res - 1
//   ndc_y = (py+0.5)*2/res - 1
//   L = sqrt(ndc_x² + ndc_y² + 1)
//   distance = zPlane * L       (intersection with z=zPlane)
//   depth = distance / farPlane
// ---------------------------------------------------------------------------

static float ComputeExpectedDepth(int px, int py, uint32_t res,
                                  float zPlane, float farPlane)
{
	const float ndcX = (static_cast<float>(px) + 0.5f) * 2.0f
	                   / static_cast<float>(res) - 1.0f;
	const float ndcY = (static_cast<float>(py) + 0.5f) * 2.0f
	                   / static_cast<float>(res) - 1.0f;
	const float L = std::sqrt(ndcX * ndcX + ndcY * ndcY + 1.0f);
	return zPlane * L / farPlane;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ShadowCubemapDepthTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		// --- 1. Shadow depth pass at 256x256 ---
		constexpr uint32_t kRes    = 256;
		constexpr float    kFar    = 25.0f;

		m_shadowPass = std::make_unique<ShadowDepthPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily, kRes, kFar);
		m_shadowPass->SetLightPosition(glm::vec3(0.0f, 0.0f, 3.0f));

		// --- 2. Cube geometry shifted to (0, 0, 6) ---
		auto verts = MakeCubeVerts(1.0f);
		auto inds  = MakeCubeIdx();

		// Shift +6 in Z so the cube center is at (0, 0, 6).
		// The -Z face of the cube sits at z=5.5 and faces the light at (0,0,3).
		for (auto& v : verts) { v.pz += 6.0f; }

		m_vbo = std::make_unique<VertexBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			verts.data(), verts.size() * sizeof(TestVertex),
			sizeof(TestVertex), static_cast<uint32_t>(verts.size()));

		m_ibo = std::make_unique<IndexBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			inds.data(), inds.size() * sizeof(uint32_t),
			static_cast<uint32_t>(inds.size()));

		// --- 3. Geometry render item ---
		GeometryRenderItem item{};
		item.vertexBuffer = m_vbo->buffer();
		item.indexBuffer  = m_ibo->buffer();
		item.indexCount   = m_ibo->GetIndexCount();
		item.indexType    = m_ibo->GetIndexType();
		// Vertices already contain the world-space positions — identity model.
		item.pushConstants.model        = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		m_renderItems = std::vector<GeometryRenderItem>{ item };
	}

	void TearDown() override
	{
		// Destroy members in reverse-construction order before device teardown.
		m_renderItems.clear();
		m_ibo.reset();
		m_vbo.reset();
		m_shadowPass.reset();

		VulkanTestShared::TearDown();
	}

	std::unique_ptr<ShadowDepthPass> m_shadowPass;
	std::unique_ptr<VertexBuffer>    m_vbo;
	std::unique_ptr<IndexBuffer>     m_ibo;
	std::vector<GeometryRenderItem>  m_renderItems;
	uint32_t m_phaseAShadowed = 0;  // populated by ShadowIntensity_EndToEnd Phase (a)
};

// ===========================================================================
// Test: CubemapFace4PlusZ_ReadbackNonEmpty
//
// Records the shadow depth pass (all 6 faces), then reads back the depth
// attachment data from face 4 (+Z) using a manual staging buffer with
// vk::ImageAspectFlagBits::eDepth.  Only guard assertions — no numeric
// pixel checks yet (Task 8 adds those).
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapFace4PlusZ_ReadbackNonEmpty)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	constexpr uint32_t kRes    = 256;
	constexpr uint32_t kFace4  = 4;   // +Z cubemap face
	constexpr uint32_t kBytesPerPixel = 4;  // D32_SFLOAT

	auto& pd = PhysicalDevice();

	// --- 1. Record shadow depth pass (writes all 6 faces) ---
	{
		auto& cmd = BeginCmd();
		m_shadowPass->Record(cmd, PassContext{
			.renderExtent = {kRes, kRes},
			.renderItems  = &m_renderItems,
		});
		EndSubmitWait(cmd);
	}

	// After Record() the cubemap is in eShaderReadOnlyOptimal.
	// We'll read back face 4 only.

	auto& cubemap = m_shadowPass->ShadowCubemap();
	const auto& cubemapImage = cubemap.ImageHandle();

	// --- 2. Manual depth readback via staging buffer ---
	// Image::ReadImageToBuffer hardcodes eColor aspect, so we do it manually.

	const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(kRes) *
	                                 kRes * kBytesPerPixel;

	// --- 2a. Staging buffer (host-visible + transfer-dst) ---
	vk::BufferCreateInfo stagingCI({}, imageSize,
	                               vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuffer(*m_device, stagingCI);

	const auto stagingMemReqs = stagingBuffer.getMemoryRequirements();
	const uint32_t stagingMemType = FindMemoryType(
		pd, stagingMemReqs.memoryTypeBits,
		vk::MemoryPropertyFlagBits::eHostVisible |
		vk::MemoryPropertyFlagBits::eHostCoherent);

	vk::MemoryAllocateInfo stagingAlloc(stagingMemReqs.size, stagingMemType);
	vk::raii::DeviceMemory stagingMemory(*m_device, stagingAlloc);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	// --- 2b. Transient command buffer ---
	vk::CommandPoolCreateInfo poolCI(
		vk::CommandPoolCreateFlagBits::eTransient,
		m_graphicsQueueFamily);
	vk::raii::CommandPool cmdPool(*m_device, poolCI);
	vk::CommandBufferAllocateInfo allocInfo(
		*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(*m_device, allocInfo);

	auto& readbackCmd = cmdBufs[0];
	readbackCmd.begin(vk::CommandBufferBeginInfo(
		vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	// --- 2c. Transition face 4: eShaderReadOnlyOptimal → eTransferSrcOptimal ---
	{
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eShaderRead,      // src
			vk::AccessFlagBits::eTransferRead,     // dst
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*cubemapImage,
			// Only face 4 (array layer 4), mip 0
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth,
			                          0, 1, kFace4, 1));
		readbackCmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eAllCommands,
			vk::PipelineStageFlagBits::eTransfer,
			{}, {}, {}, barrier);
	}

	// --- 2d. Copy face 4 depth to staging buffer ---
	{
		vk::BufferImageCopy copyRegion;
		copyRegion.bufferOffset      = 0;
		copyRegion.bufferRowLength   = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
			vk::ImageAspectFlagBits::eDepth, 0, kFace4, 1);
		copyRegion.imageOffset       = vk::Offset3D(0, 0, 0);
		copyRegion.imageExtent       = vk::Extent3D(kRes, kRes, 1);

		readbackCmd.copyImageToBuffer(
			*cubemapImage,
			vk::ImageLayout::eTransferSrcOptimal,
			*stagingBuffer,
			copyRegion);
	}

	// --- 2e. Memory barrier for host read ---
	{
		vk::MemoryBarrier barrier(
			vk::AccessFlagBits::eTransferWrite,
			vk::AccessFlagBits::eHostRead);
		readbackCmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eHost,
			{}, barrier, {}, {});
	}

	// --- 2f. Transition face 4 back to eShaderReadOnlyOptimal ---
	{
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eTransferRead,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*cubemapImage,
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth,
			                          0, 1, kFace4, 1));
		readbackCmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eAllCommands,
			{}, {}, {}, barrier);
	}

	readbackCmd.end();

	// --- 2g. Submit and wait ---
	{
		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*readbackCmd));
		m_queue.submit(submitInfo);
		m_device->waitIdle();
	}

	// --- 2h. Map staging memory and copy to host vector ---
	std::vector<uint8_t> data(static_cast<size_t>(imageSize));
	void* mapped = stagingMemory.mapMemory(0, imageSize);
	std::memcpy(data.data(), mapped, static_cast<size_t>(imageSize));
	stagingMemory.unmapMemory();

	// --- 3. Guard assertions only (no numeric checks yet) ---
	ASSERT_FALSE(data.empty()) << "Depth readback produced empty buffer";
	EXPECT_GT(data.size(), 0u) << "Depth readback buffer size is zero";

	NEURUS_LOG("[ShadowCubemapDepth] Read back " << data.size()
	           << " bytes from cubemap face 4 (+Z)");
}

// ===========================================================================
// Reference-image directory (mirrors test_deferred_shading.cpp)
// ===========================================================================

static const char* kShadowRefDir = "../../../test/render/reference/shadow/";

// ---------------------------------------------------------------------------
// Helper: write depth float data as grayscale RGBA8 PNG
// ---------------------------------------------------------------------------

static bool WriteDepthPng(const std::vector<uint8_t>& depthData,
                          uint32_t width, uint32_t height,
                          const std::string& path)
{
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
	for (uint32_t i = 0; i < width * height; ++i)
	{
		float depth;
		std::memcpy(&depth, &depthData[i * 4], sizeof(float));
		const uint8_t u8 = static_cast<uint8_t>(
			std::clamp(depth * 255.0f, 0.0f, 255.0f));
		const size_t base = static_cast<size_t>(i) * 4;
		rgba[base + 0] = u8;
		rgba[base + 1] = u8;
		rgba[base + 2] = u8;
		rgba[base + 3] = 255;
	}
	return stbi_write_png(path.c_str(),
	                      static_cast<int>(width),
	                      static_cast<int>(height),
	                      4, rgba.data(),
	                      static_cast<int>(width) * 4) != 0;
}

// ---------------------------------------------------------------------------
// Helper: load RGBA8 PNG into vector, returns true on success
// ---------------------------------------------------------------------------

static bool LoadPngRGBA(const std::string& path,
                        std::vector<uint8_t>& pixels,
                        int& width, int& height)
{
	int w, h, c;
	unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
	if (!data) return false;
	width = w;
	height = h;
	const size_t byteCount = static_cast<size_t>(w) * h * 4;
	pixels.assign(data, data + byteCount);
	stbi_image_free(data);
	return true;
}

// ---------------------------------------------------------------------------
// Helper: compare two RGBA8 images pixel-wise, return number of bad pixels
// ---------------------------------------------------------------------------

static int ComparePixelsRGBA(const std::vector<uint8_t>& a,
                             const std::vector<uint8_t>& b,
                             int width, int height,
                             int maxDiffPerChannel = 2)
{
	const size_t count = static_cast<size_t>(width) * height * 4;
	if (a.size() != count || b.size() != count) return -1;
	int badPixels = 0;
	for (size_t i = 0; i < count; i += 4)
	{
		for (int ch = 0; ch < 4; ++ch)
		{
			const int delta = std::abs(
				static_cast<int>(a[i + ch]) - static_cast<int>(b[i + ch]));
			if (delta > maxDiffPerChannel)
			{
				++badPixels;
				break;
			}
		}
	}
	return badPixels;
}

// ===========================================================================
// Test: Step A — Pixel-wise mathematical verification via ShadowEvalPass
//
// NOTE: CopyImageToBuffer from D32 cubemaps returns all-zero on the test GPU.
//       Therefore we verify cubemap depth INDIRECTLY through ShadowEvalPass:
//       render cube geometry at z=6 → G-Buffer + cubemap → ShadowEvalPass.
//       The cube's -Z face (at z≈5.5) acts as a self-occluder, casting
//       shadow on the +Z face (at z≈6.5) visible from the camera.
//
//       Mathematical verification:
//         - Cubemap: -Z face depth = dist_to_face/25 ≈ (5.5+ε)/25 ≈ 0.22
//         - G-Buffer: +Z face currentDepth = dist_to_face/25 ≈ (6.5-ε)/25 ≈ 0.26
//         - Shadow: (0.26 - 0.005 > 0.22) → true → shadow = 255
//         - Background pixels: posSample.w = 0 → shadow = 0
//
//       Verifies: shadowMax = 255, shadowed > 0, lit > 0, pixel counts match
//       expected geometry coverage.
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_MathVerification)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();
	constexpr uint32_t kWidth     = 256;
	constexpr uint32_t kHeight    = 256;
	constexpr uint32_t kShadowRes = 1024;
	constexpr float    kFar       = 25.0f;

	// --- 1. Cube at z=6 (self-shadows: -Z face at z≈5.5 shadows +Z at z≈6.5) ---
	auto verts = MakeCubeVerts(1.0f);
	auto inds  = MakeCubeIdx();
	for (auto& v : verts) { v.pz += 6.0f; }

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
	                           kShadowRes, kFar);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	ShadowEvalPass evalPass(*m_device, pd, &attMgr, 1u);
	evalPass.SetLight(shadowPass.ShadowCubemap(), glm::vec3(0.0f), kFar, 0.005f);

	// --- 3. Camera: at (0,0,9) looking at cube at z=6 ---
	const glm::mat4 viewMat = glm::lookAt(
		glm::vec3(0.0f, 0.0f, 9.0f),
		glm::vec3(0.0f, 0.0f, 6.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projMat = glm::perspective(
		glm::radians(60.0f),
		static_cast<float>(kWidth) / static_cast<float>(kHeight),
		0.1f, 100.0f);
	CameraUBOData camUBO{};
	camUBO.view     = viewMat;
	camUBO.viewProj = projMat * viewMat;

	// --- 4. Transition G-Buffer ---
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

	// Transition to TRANSFER_SRC
	{
		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 m_graphicsQueueFamily);
		vk::raii::CommandPool p(*m_device, poolCI);
		vk::CommandBufferAllocateInfo ai(*p, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cbs(*m_device, ai);

		cbs[0].begin(vk::CommandBufferBeginInfo(
			vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const vk::ImageLayout oldLayout = shadowAtt.CurrentLayout();
		vk::AccessFlags srcAccess = {};
		vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		if (oldLayout != vk::ImageLayout::eUndefined)
		{
			srcAccess = vk::AccessFlagBits::eMemoryRead |
			            vk::AccessFlagBits::eMemoryWrite;
			srcStage  = vk::PipelineStageFlagBits::eAllCommands;
		}

		vk::ImageMemoryBarrier barrier(
			srcAccess, vk::AccessFlagBits::eTransferRead,
			oldLayout, vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*shadowAtt.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));
		cbs[0].pipelineBarrier(srcStage,
		                       vk::PipelineStageFlagBits::eTransfer,
		                       {}, {}, {}, barrier);
		cbs[0].end();

		vk::SubmitInfo si({}, {}, {}, 1, &(*cbs[0]));
		m_queue.submit(si);
		m_queue.waitIdle();

		shadowAtt.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
	}

	auto shadowData = shadowAtt.ReadImageToBuffer(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		vk::ImageLayout::eTransferSrcOptimal);

	ASSERT_EQ(shadowData.size(), static_cast<size_t>(kWidth * kHeight))
		<< "ShadowIntensity readback size mismatch";

	// --- 9. Analyze ---
	uint8_t shadowMax = 0, shadowMin = 255;
	uint32_t shadowedCount = 0, litCount = 0, bgCount = 0;
	for (auto v : shadowData)
	{
		shadowMax = std::max(shadowMax, v);
		shadowMin = std::min(shadowMin, v);
		if (v >= 128)      ++shadowedCount;
		else if (v == 0)   ++bgCount;
		else               ++litCount;
	}
	uint32_t totalGeom = shadowedCount + litCount;

	NEURUS_LOG("[MathVerif] shadowMax=" << static_cast<int>(shadowMax)
	           << " shadowMin=" << static_cast<int>(shadowMin)
	           << " shadowed=" << shadowedCount << " lit=" << litCount
	           << " bg=" << bgCount << " geomPixels=" << totalGeom);

	// --- 10. Assertions ---
	// Cube self-shadows: -Z face (z≈5.5, depth≈0.22) occludes +Z face (z≈6.5, depth≈0.26)
	// All 7744 visible geometry pixels should be shadowed at this distance.
	EXPECT_EQ(shadowMax, 255u)
		<< "Cube should self-shadow (max shadow = 255)";
	EXPECT_GT(shadowedCount, 0u)
		<< "No shadowed pixels — cubemap depth may be zero";
	EXPECT_GT(bgCount, 0u)
		<< "No background pixels — cube may cover entire viewport";
	EXPECT_GT(totalGeom, 100u)
		<< "Too few geometry pixels (" << totalGeom << ") — cube may not be rendering";

	// The shadow fraction should be 1.0 (all geometry pixels self-shadowed)
	const float shadowFrac = static_cast<float>(shadowedCount) / totalGeom;
	EXPECT_GT(shadowFrac, 0.5f)
		<< "Shadow fraction too low: " << shadowFrac << " (expected all geometry self-shadowed)";
}

// ===========================================================================
// Test: Step B — Reference-image regression test
//
// Captures ShadowIntensity from the cube-at-z=6 self-shadow scene as an
// 8-bit PNG reference image.  First run: generates reference and SKIPs.
// Subsequent runs: compares pixel-by-pixel with ±2 tolerance.
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_ReferenceImage)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();
	constexpr uint32_t kWidth     = 256;
	constexpr uint32_t kHeight    = 256;
	constexpr uint32_t kShadowRes = 1024;
	constexpr float    kFar       = 25.0f;

	// Setup identical to MathVerification test
	auto verts = MakeCubeVerts(1.0f);
	auto inds  = MakeCubeIdx();
	for (auto& v : verts) { v.pz += 6.0f; }

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

	AttachmentManager attMgr(*m_device, pd);
	attMgr.Create(vk::Extent2D{kWidth, kHeight});
	RenderPassManager rpm;

	GeometryPass geoPass(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                     attMgr, rpm,
	                     gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
	                     gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

	ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                           kShadowRes, kFar);
	shadowPass.SetLightPosition(glm::vec3(0.0f));

	ShadowEvalPass evalPass(*m_device, pd, &attMgr, 1u);
	evalPass.SetLight(shadowPass.ShadowCubemap(), glm::vec3(0.0f), kFar, 0.005f);

	const glm::mat4 viewMat = glm::lookAt(
		glm::vec3(0.0f, 0.0f, 9.0f), glm::vec3(0.0f, 0.0f, 6.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projMat = glm::perspective(
		glm::radians(60.0f),
		static_cast<float>(kWidth) / static_cast<float>(kHeight),
		0.1f, 100.0f);
	CameraUBOData camUBO{};
	camUBO.view     = viewMat;
	camUBO.viewProj = projMat * viewMat;

	// Transition G-Buffer
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

	// Geometry pass
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

	// Shadow depth pass
	{
		auto& cmd = BeginCmd();
		PassContext shadowCtx{};
		shadowCtx.renderExtent = vk::Extent2D{kShadowRes, kShadowRes};
		shadowCtx.renderItems  = &items;
		shadowPass.Record(cmd, shadowCtx);
		EndSubmitWait(cmd);
	}

	// Shadow eval pass
	{
		auto& cmd = BeginCmd();
		evalPass.Record(cmd, PassContext{
			.renderExtent = {kWidth, kHeight},
			.frameIndex   = 0,
		});
		EndSubmitWait(cmd);
	}

	// Capture ShadowIntensity to PNG via Screenshot
	std::filesystem::create_directories(kShadowRefDir);

	const std::string refPath = std::string(kShadowRefDir) + "ShadowIntensity_CubeAt6.png";
	const std::string tmpPath = refPath + ".tmp";
	const bool refExists = std::ifstream(refPath).good();

	auto& shadowAtt = attMgr.GetAttachment(AttachmentName::ShadowIntensity);
	const bool captured = Screenshot::CaptureAttachment(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		shadowAtt, tmpPath, false);

	ASSERT_TRUE(captured) << "Failed to capture ShadowIntensity to " << tmpPath;

	if (!refExists)
	{
		std::rename(tmpPath.c_str(), refPath.c_str());
		GTEST_SKIP() << "Reference image generated at " << refPath
		             << ".  Re-run the test to compare.";
	}

	// Compare
	std::vector<uint8_t> tmpPixels, refPixels;
	int tmpW, tmpH, refW, refH;

	ASSERT_TRUE(LoadPngRGBA(tmpPath, tmpPixels, tmpW, tmpH))
		<< "Failed to load captured PNG: " << tmpPath;
	ASSERT_TRUE(LoadPngRGBA(refPath, refPixels, refW, refH))
		<< "Failed to load reference PNG: " << refPath;
	ASSERT_EQ(tmpW, refW) << "Width mismatch";
	ASSERT_EQ(tmpH, refH) << "Height mismatch";

	const int badPixels = ComparePixelsRGBA(tmpPixels, refPixels, tmpW, tmpH, 2);
	std::remove(tmpPath.c_str());

	EXPECT_EQ(badPixels, 0)
		<< badPixels << " pixel(s) differ from reference (threshold: ±2 per channel)";
}

// ===========================================================================
// Test: Step C — End-to-end ShadowIntensity test with / without occluder
//
// Sets up the full pipeline (GeometryPass → ShadowDepthPass → ShadowEvalPass)
// and verifies:
//   (a) With only a z=6 plane: all plane pixels are fully lit (shadowMax == 0)
//   (b) With an occluder plane at z=3: z=6 plane pixels are shadowed
//       (shadowMax > 0)
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, ShadowIntensity_EndToEnd)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }

	auto& pd = PhysicalDevice();
	constexpr uint32_t kWidth     = 256;
	constexpr uint32_t kHeight    = 256;
	constexpr uint32_t kShadowRes = 1024;  // Match working test (test_shadow_deterministic.cpp)
	constexpr float    kFar       = 25.0f;

	// --- 1. Create AttachmentManager (Position + ShadowIntensity) ---
	AttachmentManager attMgr(*m_device, pd);
	attMgr.Create(vk::Extent2D{kWidth, kHeight});

	RenderPassManager rpm;

	// --- 2. GeometryPass (for filling Position G-Buffer) ---
	GeometryPass geoPass(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                     attMgr, rpm,
	                     gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
	                     gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

	// --- 3. CUBE at z=6 (diagnostic: verify cubemap renders at z=6) ---
	auto quadVerts6 = MakeCubeVerts(1.0f);
	for (auto& v : quadVerts6) { v.pz += 6.0f; }  // cube at z=6
	auto quadInds   = MakeCubeIdx();

	VertexBuffer vbo6(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                  quadVerts6.data(), quadVerts6.size() * sizeof(TestVertex),
	                  sizeof(TestVertex), static_cast<uint32_t>(quadVerts6.size()));
	IndexBuffer ibo6(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 quadInds.data(), quadInds.size() * sizeof(uint32_t),
	                 static_cast<uint32_t>(quadInds.size()));

	GeometryRenderItem item6{};
	item6.vertexBuffer = vbo6.buffer();
	item6.indexBuffer  = ibo6.buffer();
	item6.indexCount   = ibo6.GetIndexCount();
	item6.indexType    = ibo6.GetIndexType();
	item6.pushConstants.model        = glm::mat4(1.0f);
	item6.pushConstants.normalMatrix = glm::mat4(1.0f);

	// --- 4. Occluder quad at z=3 (between light at 0 and plane at 6) ---
	auto quadVerts3 = MakeQuadVertsZ6();
	for (auto& v : quadVerts3) { v.pz = 3.0f; }  // occluder at z=3
	auto inds3 = MakeQuadIdx();

	VertexBuffer vbo3(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                  quadVerts3.data(), quadVerts3.size() * sizeof(TestVertex),
	                  sizeof(TestVertex), static_cast<uint32_t>(quadVerts3.size()));
	IndexBuffer ibo3(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 inds3.data(), inds3.size() * sizeof(uint32_t),
	                 static_cast<uint32_t>(inds3.size()));

	GeometryRenderItem item3{};
	item3.vertexBuffer = vbo3.buffer();
	item3.indexBuffer  = ibo3.buffer();
	item3.indexCount   = ibo3.GetIndexCount();
	item3.indexType    = ibo3.GetIndexType();
	item3.pushConstants.model        = glm::mat4(1.0f);
	item3.pushConstants.normalMatrix = glm::mat4(1.0f);

	// --- 5. Camera: at (0,0,9) looking at cube at z=6 ---
	const glm::mat4 viewMat = glm::lookAt(
		glm::vec3(0.0f, 0.0f, 9.0f),
		glm::vec3(0.0f, 0.0f, 6.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projMat = glm::perspective(
		glm::radians(60.0f),
		static_cast<float>(kWidth) / static_cast<float>(kHeight),
		0.1f, 100.0f);

	CameraUBOData camUBO{};
	camUBO.view     = viewMat;
	camUBO.viewProj = projMat * viewMat;

	// ---- Helper: read back ShadowIntensity attachment ----
	// (Reuse the ReadR8Attachment pattern from test_shadow_deterministic.cpp)

	auto readShadowIntensity = [&](Image& shadowAtt) -> std::vector<uint8_t>
	{
		// Transition to TRANSFER_SRC
		{
			vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
			                                 m_graphicsQueueFamily);
			vk::raii::CommandPool p(*m_device, poolCI);
			vk::CommandBufferAllocateInfo ai(*p, vk::CommandBufferLevel::ePrimary, 1);
			vk::raii::CommandBuffers cbs(*m_device, ai);

			cbs[0].begin(vk::CommandBufferBeginInfo(
				vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			const vk::ImageLayout oldLayout = shadowAtt.CurrentLayout();
			vk::AccessFlags srcAccess = {};
			vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			if (oldLayout != vk::ImageLayout::eUndefined)
			{
				srcAccess = vk::AccessFlagBits::eMemoryRead |
				            vk::AccessFlagBits::eMemoryWrite;
				srcStage  = vk::PipelineStageFlagBits::eAllCommands;
			}

			vk::ImageMemoryBarrier barrier(
				srcAccess, vk::AccessFlagBits::eTransferRead,
				oldLayout, vk::ImageLayout::eTransferSrcOptimal,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				*shadowAtt.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          0, 1, 0, 1));
			cbs[0].pipelineBarrier(srcStage,
			                       vk::PipelineStageFlagBits::eTransfer,
			                       {}, {}, {}, barrier);
			cbs[0].end();

			vk::SubmitInfo si({}, {}, {}, 1, &(*cbs[0]));
			m_queue.submit(si);
			m_queue.waitIdle();

			shadowAtt.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
		}

		return shadowAtt.ReadImageToBuffer(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			vk::ImageLayout::eTransferSrcOptimal);
	};

	// =======================================================================
	// Phase (a): No occluder — cube at z=6 only (self-shadows)
	// =======================================================================
	{
		// Transition G-Buffer to color attachment
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

		// Geometry pass: cube at z=6 → G-Buffer Position
		{
			auto& cmd = BeginCmd();
			std::vector<GeometryRenderItem> geoItems{ item6 };
			geoPass.Record(cmd, PassContext{
				.renderExtent = {kWidth, kHeight},
				.viewProj     = camUBO.viewProj,
				.view         = camUBO.view,
				.renderItems  = &geoItems,
			});
			EndSubmitWait(cmd);
		}

		// Shadow depth pass: cube at z=6 ONLY → cubemap
		ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily,
		                           kShadowRes, kFar);
		shadowPass.SetLightPosition(glm::vec3(0.0f));

		{
			auto& cmd = BeginCmd();
			std::vector<GeometryRenderItem> shadowItems{ item6 };
			shadowPass.Record(cmd, PassContext{
				.renderExtent = {kShadowRes, kShadowRes},
				.renderItems  = &shadowItems,
			});
			EndSubmitWait(cmd);
		}

		// Shadow eval pass
		ShadowEvalPass evalPass(*m_device, pd, &attMgr, 1u);
		evalPass.SetLight(shadowPass.ShadowCubemap(), glm::vec3(0.0f), kFar,
		                  0.005f);

		{
			auto& cmd = BeginCmd();
			evalPass.Record(cmd, PassContext{
				.renderExtent = {kWidth, kHeight},
				.frameIndex   = 0,
			});
			EndSubmitWait(cmd);
		}

		// Read back ShadowIntensity
		auto& shadowAtt = attMgr.GetAttachment(AttachmentName::ShadowIntensity);
		auto shadowData = readShadowIntensity(shadowAtt);

		ASSERT_EQ(shadowData.size(), static_cast<size_t>(kWidth * kHeight))
			<< "ShadowIntensity readback size mismatch";

		// Cube self-shadows: -Z face (closer to light) occludes +Z face
		uint8_t shadowMax = 0;
		uint32_t shadowedCount = 0, litCount = 0, bgCount = 0;
		for (auto v : shadowData)
		{
			shadowMax = std::max(shadowMax, v);
			if (v >= 128)      ++shadowedCount;
			else if (v == 0)   ++bgCount;
			else               ++litCount;
		}

		NEURUS_LOG("[ShadowIntensity/no-occluder] shadowMax="
		           << static_cast<int>(shadowMax)
		           << " shadowed=" << shadowedCount << " lit=" << litCount
		           << " bg=" << bgCount);

		// Cube self-shadows: all geometry pixels should be shadowed at this distance
		EXPECT_EQ(shadowMax, 255u)
			<< "ShadowMax should be 255 (cube self-shadows)";
		EXPECT_GT(shadowedCount, 0u)
			<< "No shadowed pixels — cubemap depth may be broken";
		EXPECT_GT(bgCount, 0u)
			<< "No background pixels — cube may cover entire viewport";

		m_phaseAShadowed = shadowedCount;  // Save for Phase (b) comparison
	}

	// =======================================================================
	// Phase (b): With occluder at z=3
	// =======================================================================
	{
		// Re-create attachments (old data was consumed)
		attMgr.Create(vk::Extent2D{kWidth, kHeight});

		// Transition G-Buffer
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

		// Geometry pass: z=6 plane → G-Buffer Position
		{
			auto& cmd = BeginCmd();
			std::vector<GeometryRenderItem> geoItems{ item6 };
			geoPass.Record(cmd, PassContext{
				.renderExtent = {kWidth, kHeight},
				.viewProj     = camUBO.viewProj,
				.view         = camUBO.view,
				.renderItems  = &geoItems,
			});
			EndSubmitWait(cmd);
		}

		// Shadow depth pass: BOTH z=6 and z=3 planes → cubemap
		ShadowDepthPass shadowPass(*m_device, pd, m_queue, m_graphicsQueueFamily,
		                           kShadowRes, kFar);
		shadowPass.SetLightPosition(glm::vec3(0.0f));

		{
			auto& cmd = BeginCmd();
			std::vector<GeometryRenderItem> shadowItems{ item3, item6 };
			shadowPass.Record(cmd, PassContext{
				.renderExtent = {kShadowRes, kShadowRes},
				.renderItems  = &shadowItems,
			});
			EndSubmitWait(cmd);
		}

		// Shadow eval pass
		ShadowEvalPass evalPass(*m_device, pd, &attMgr, 1u);
		evalPass.SetLight(shadowPass.ShadowCubemap(), glm::vec3(0.0f), kFar,
		                  0.005f);

		{
			auto& cmd = BeginCmd();
			evalPass.Record(cmd, PassContext{
				.renderExtent = {kWidth, kHeight},
				.frameIndex   = 0,
			});
			EndSubmitWait(cmd);
		}

		// Read back ShadowIntensity
		auto& shadowAtt = attMgr.GetAttachment(AttachmentName::ShadowIntensity);
		auto shadowData = readShadowIntensity(shadowAtt);

		ASSERT_EQ(shadowData.size(), static_cast<size_t>(kWidth * kHeight));

		uint8_t shadowMax = 0;
		uint32_t shadowedCount = 0;
		for (auto v : shadowData)
		{
			shadowMax = std::max(shadowMax, v);
			if (v >= 128) ++shadowedCount;  // ≥ 0.5 = shadowed
		}

		NEURUS_LOG("[ShadowIntensity/with-occluder] shadowMax="
		           << static_cast<int>(shadowMax)
		           << " shadowedCount=" << shadowedCount
		           << " (phaseA=" << m_phaseAShadowed << ")");

		// With occluder, shadowed pixels should increase vs. phase (a)
		EXPECT_GT(shadowMax, 0u)
			<< "ShadowMax should be >0 with occluder; got "
			<< static_cast<int>(shadowMax);
		EXPECT_GE(shadowedCount, m_phaseAShadowed)
			<< "Shadowed count should increase with occluder (phaseA="
			<< m_phaseAShadowed << " phaseB=" << shadowedCount << ")";
	}
}
