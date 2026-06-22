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

#include "Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
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
