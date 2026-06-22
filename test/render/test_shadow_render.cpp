/**
 * @file test_shadow_render.cpp
 * @brief Reference-image regression test for point light shadow mapping
 *        using the Cornell Box scene.
 *
 * Renders the Cornell Box through GeometryPass → ShadowDepthPass →
 * ShadowEvalPass and captures the ShadowIntensity output as a PNG.
 * Compared against a stored reference image on subsequent runs.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"
#include "shared/TestCornellBox.h"

#include "render/passes/AttachmentManager.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/PassContext.h"
#include "render/passes/RenderPassManager.h"
#include "render/passes/ShadowDepthPass.h"
#include "render/passes/ShadowEvalPass.h"
#include "render/Screenshot.h"
#include "render/VulkanBuffer.h"
#include "render/Image.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include "Log.h"

#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <gbuffer.vert.h>
#include <gbuffer.frag.h>

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Diagnostic helpers — read back raw R8 attachment and cubemap depth
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

struct ReadDepthResult
{
	std::vector<float> data;   // decoded float32 depth values
	float minVal = 1.0f;
	float maxVal = 0.0f;
	uint32_t uniqueCount = 0;
	uint32_t nonOneCount = 0;  // pixels with depth < 0.999 (i.e. NOT clear value)
};

/**
 * @brief Reads back the depth attachment from a single cubemap face.
 *
 * Creates a staging buffer, transitions face `faceIndex` to TRANSFER_SRC,
 * copies it, and decodes the D32_SFLOAT values.
 */
static ReadDepthResult ReadCubemapDepthFace(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue queue,
	uint32_t queueFamilyIndex,
	Image& cubemap,
	uint32_t faceIndex,
	uint32_t resolution)
{
	ReadDepthResult result{};
	constexpr uint32_t kBytesPerPixel = 4;  // D32_SFLOAT
	const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(resolution) *
	                                 resolution * kBytesPerPixel;

	// Find memory type helper (same as Image::FindMemoryType)
	auto findMemType = [&](uint32_t typeFilter, vk::MemoryPropertyFlags props) -> uint32_t {
		const auto memProps = physicalDevice.getMemoryProperties();
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1u << i)) &&
			    (memProps.memoryTypes[i].propertyFlags & props) == props)
				return i;
		}
		throw std::runtime_error("ReadCubemapDepthFace: no suitable memory type");
	};

	// Staging buffer
	vk::BufferCreateInfo stagingCI({}, imageSize, vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuffer(device, stagingCI);
	auto stagingReqs = stagingBuffer.getMemoryRequirements();
	uint32_t stagingType = findMemType(stagingReqs.memoryTypeBits,
	                                    vk::MemoryPropertyFlagBits::eHostVisible |
	                                    vk::MemoryPropertyFlagBits::eHostCoherent);
	vk::MemoryAllocateInfo stagingAlloc(stagingReqs.size, stagingType);
	vk::raii::DeviceMemory stagingMemory(device, stagingAlloc);
	stagingBuffer.bindMemory(*stagingMemory, 0);

	// Command buffer
	vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient, queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, poolCI);
	vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocInfo);
	auto& cb = cmdBufs[0];
	cb.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	// Transition face to TRANSFER_SRC
	{
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eShaderRead,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*cubemap.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, faceIndex, 1));
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
		                   vk::PipelineStageFlagBits::eTransfer,
		                   {}, {}, {}, barrier);
	}

	// Copy to staging
	{
		vk::BufferImageCopy copyRegion;
		copyRegion.bufferOffset      = 0;
		copyRegion.bufferRowLength   = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
			vk::ImageAspectFlagBits::eDepth, 0, faceIndex, 1);
		copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
		copyRegion.imageExtent = vk::Extent3D(resolution, resolution, 1);
		cb.copyImageToBuffer(*cubemap.ImageHandle(),
		                     vk::ImageLayout::eTransferSrcOptimal,
		                     *stagingBuffer, copyRegion);
	}

	// Host read barrier
	{
		vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite,
		                          vk::AccessFlagBits::eHostRead);
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                   vk::PipelineStageFlagBits::eHost,
		                   {}, barrier, {}, {});
	}

	// Transition face back to SHADER_READ_ONLY
	{
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eTransferRead,
			vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*cubemap.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, faceIndex, 1));
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                   vk::PipelineStageFlagBits::eAllCommands,
		                   {}, {}, {}, barrier);
	}

	cb.end();
	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cb));
	queue.submit(submitInfo);
	queue.waitIdle();

	// Decode D32_SFLOAT values
	const uint32_t pixelCount = resolution * resolution;
	result.data.resize(pixelCount);
	std::vector<float> allVals;
	allVals.reserve(pixelCount);
	float* mapped = static_cast<float*>(stagingMemory.mapMemory(0, imageSize));
	for (uint32_t i = 0; i < pixelCount; ++i)
	{
		result.data[i] = mapped[i];
		allVals.push_back(mapped[i]);
	}
	stagingMemory.unmapMemory();

	// Compute stats
	if (!allVals.empty())
	{
		std::sort(allVals.begin(), allVals.end());
		result.minVal = allVals.front();
		result.maxVal = allVals.back();
		result.nonOneCount = static_cast<uint32_t>(
			std::count_if(allVals.begin(), allVals.end(),
			              [](float v) { return v < 0.999f; }));
		auto last = std::unique(allVals.begin(), allVals.end(),
		                        [](float a, float b) { return std::abs(a - b) < 0.00001f; });
		result.uniqueCount = static_cast<uint32_t>(std::distance(allVals.begin(), last));
	}

	return result;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ShadowRenderTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		// Attachment manager + render pass manager
		m_attachmentManager = std::make_unique<AttachmentManager>(*m_device, pd);
		m_attachmentManager->Create({kRenderWidth, kRenderHeight});
		m_renderPassManager = std::make_unique<RenderPassManager>();

		// Geometry pass
		m_geometryPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			*m_attachmentManager, *m_renderPassManager,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		// Shadow depth pass (1024x1024 cubemap)
		m_shadowDepthPass = std::make_unique<ShadowDepthPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			1024u, 25.0f);

		// Shadow eval pass
		m_shadowEvalPass = std::make_unique<ShadowEvalPass>(
			*m_device, pd, m_attachmentManager.get(), 1u);
	}

	void TearDown() override
	{
		m_shadowEvalPass.reset();
		m_shadowDepthPass.reset();
		m_geometryPass.reset();
		m_renderPassManager.reset();
		m_attachmentManager.reset();
		VulkanTestShared::TearDown();
	}

	CameraUBOData ComputeCameraUBO(Camera& cam) const
	{
		CameraUBOData ubo;
		ubo.view = cam.GetViewMatrix();
		ubo.viewProj = cam.GetProjectionMatrix() * ubo.view;
		return ubo;
	}

	void TransitionGbufferToColorAttachment()
	{
		auto& cmd = BeginCmd();
		const std::array<AttachmentName, 4> colorAtts = {
			AttachmentName::Position, AttachmentName::Normal,
			AttachmentName::Albedo, AttachmentName::MetallicRoughness,
		};
		for (const auto& att : colorAtts)
		{
			m_attachmentManager->GetAttachment(att).TransitionLayout(
				cmd, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal);
		}
		m_attachmentManager->GetAttachment(AttachmentName::Depth).TransitionLayout(
			cmd, vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);
		EndSubmitWait(cmd);
	}

	static std::string ReferencePath(const char* name)
	{
		return std::string("../../../test/render/reference/shadow/") + name;
	}

	static bool LoadPng(const std::string& path,
	                    std::vector<uint8_t>& pixels,
	                    int& w, int& h, int& c)
	{
		int iw, ih, ic;
		unsigned char* data = stbi_load(path.c_str(), &iw, &ih, &ic, 4);
		if (!data) return false;
		w = iw; h = ih; c = 4;
		size_t byteCount = static_cast<size_t>(w) * h * 4;
		pixels.assign(data, data + byteCount);
		stbi_image_free(data);
		return true;
	}

	static int ComparePixels(const std::vector<uint8_t>& a,
	                         const std::vector<uint8_t>& b,
	                         int w, int h, int maxDiff = 2)
	{
		size_t count = static_cast<size_t>(w) * h * 4;
		if (a.size() != count || b.size() != count) return -1;
		int bad = 0;
		for (size_t i = 0; i < count; i += 4)
		{
			for (int ch = 0; ch < 4; ++ch)
			{
				if (std::abs(static_cast<int>(a[i+ch]) - static_cast<int>(b[i+ch])) > maxDiff)
				{ ++bad; break; }
			}
		}
		return bad;
	}

	std::unique_ptr<AttachmentManager> m_attachmentManager;
	std::unique_ptr<RenderPassManager> m_renderPassManager;
	std::unique_ptr<GeometryPass>      m_geometryPass;
	std::unique_ptr<ShadowDepthPass>   m_shadowDepthPass;
	std::unique_ptr<ShadowEvalPass>    m_shadowEvalPass;
};

// ===========================================================================
// Cornell Box Shadow Reference Image Test
// ===========================================================================

TEST_F(ShadowRenderTest, CornellBox_ShadowIntensity_MatchesReference)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// --- 1. Load Cornell Box ---
	auto cb = test::LoadCornellBox(*m_device, pd, m_queue, m_graphicsQueueFamily);
	ASSERT_GT(cb.renderItems.size(), 0u) << "No Cornell Box meshes loaded";

	cb.camera->ChangeCamRatio(static_cast<float>(kRenderWidth),
	                          static_cast<float>(kRenderHeight));
	const CameraUBOData camUBO = ComputeCameraUBO(*cb.camera);

	// --- 2. Transition G-Buffer & record geometry pass ---
	TransitionGbufferToColorAttachment();
	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camUBO.viewProj,
			.view = camUBO.view,
			.renderItems = &cb.renderItems,
		});
		EndSubmitWait(cmd);
	}

	// --- 3. Record shadow depth pass ---
	//     Use a point light positioned where TestCornellBox places the light.
	glm::vec3 lightPos = cb.light->GetPosition();
	m_shadowDepthPass->SetLightPosition(lightPos);

	{
		auto& cmd = BeginCmd();
		PassContext shadowCtx{};
		shadowCtx.renderExtent = vk::Extent2D{1024, 1024};
		shadowCtx.renderItems  = &cb.renderItems;
		m_shadowDepthPass->Record(cmd, shadowCtx);
		EndSubmitWait(cmd);
	}

	// --- 4. Configure and record shadow eval pass ---
	m_shadowEvalPass->SetLight(m_shadowDepthPass->ShadowCubemap(),
	                            lightPos, 25.0f, 0.005f);

	{
		auto& cmd = BeginCmd();
		m_shadowEvalPass->Record(cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.frameIndex = 0,
		});
		EndSubmitWait(cmd);
	}

	// --- 4b. DIAGNOSTIC: Raw ShadowIntensity readback ---
	{
		auto& shadowAtt = m_attachmentManager->GetAttachment(
			AttachmentName::ShadowIntensity);
		auto r8Result = ReadR8Attachment(*m_device, pd, m_queue,
		                                  m_graphicsQueueFamily, shadowAtt);
		ASSERT_FALSE(r8Result.data.empty()) << "Failed to read back ShadowIntensity";

		uint32_t count255 = 0, count0 = 0, countOther = 0;
		for (auto v : r8Result.data)
		{
			if (v == 255) ++count255;
			else if (v == 0) ++count0;
			else ++countOther;
		}

		NEURUS_LOG("[ShadowRenderDiag] ShadowIntensity raw readback:");
		NEURUS_LOG("  size=" << r8Result.data.size()
		           << " min=" << static_cast<int>(r8Result.minVal)
		           << " max=" << static_cast<int>(r8Result.maxVal)
		           << " allSame=" << (r8Result.allSame ? "YES" : "NO"));
		NEURUS_LOG("  count255(shadowed)=" << count255
		           << " count0(lit)=" << count0
		           << " countOther=" << countOther);

		EXPECT_FALSE(r8Result.allSame)
			<< "ShadowIntensity is uniform (all " << static_cast<int>(r8Result.minVal)
			<< "). Shadow eval pass may be broken.";
	}

	// --- 4c. DIAGNOSTIC: Read back cubemap depth face 4 (+Z) ---
	{
		auto& cubemap = m_shadowDepthPass->ShadowCubemap();
		const uint32_t shadowRes = m_shadowDepthPass->Resolution();
		auto depthResult = ReadCubemapDepthFace(*m_device, pd, m_queue,
		                                         m_graphicsQueueFamily,
		                                         cubemap, 4u, shadowRes);
		NEURUS_LOG("[ShadowRenderDiag] Cubemap face 4 (+Z) depth readback:");
		NEURUS_LOG("  resolution=" << shadowRes
		           << " pixelCount=" << depthResult.data.size());
		NEURUS_LOG("  minDepth=" << depthResult.minVal
		           << " maxDepth=" << depthResult.maxVal
		           << " uniqueCount=" << depthResult.uniqueCount
		           << " nonOneCount=" << depthResult.nonOneCount);

		if (depthResult.nonOneCount == 0)
		{
			NEURUS_LOG("  *** CUBEMAP IS ALL 1.0 (clear value) — NO GEOMETRY RENDERED ***");
		}
		else
		{
			NEURUS_LOG("  *** Cubemap contains " << depthResult.nonOneCount
			           << " non-clear pixels — geometry IS being rendered ***");
			NEURUS_LOG("  depth range: [" << depthResult.minVal << ", "
			           << depthResult.maxVal << "]");
		}
	}

	// Also check face 0 (+X) — should see left_box at distance ~0.85 from light
	{
		auto& cubemap = m_shadowDepthPass->ShadowCubemap();
		const uint32_t shadowRes = m_shadowDepthPass->Resolution();
		auto depthResult = ReadCubemapDepthFace(*m_device, pd, m_queue,
		                                         m_graphicsQueueFamily,
		                                         cubemap, 0u, shadowRes);
		NEURUS_LOG("[ShadowRenderDiag] Cubemap face 0 (+X) depth readback:");
		NEURUS_LOG("  minDepth=" << depthResult.minVal
		           << " maxDepth=" << depthResult.maxVal
		           << " uniqueCount=" << depthResult.uniqueCount
		           << " nonOneCount=" << depthResult.nonOneCount);

		// Sample a few specific pixels for raw byte verification
		if (depthResult.data.size() >= 100)
		{
			const uint8_t* raw = reinterpret_cast<const uint8_t*>(depthResult.data.data());
			bool anyNonZero = false;
			for (size_t i = 0; i < 16; ++i)
			{
				if (raw[i] != 0) { anyNonZero = true; break; }
			}
			NEURUS_LOG("  First 16 bytes all zero: " << (anyNonZero ? "NO" : "YES")
			           << " (sample floats: " << depthResult.data[0]
			           << ", " << depthResult.data[1]
			           << ", " << depthResult.data[2] << ")");
		}
	}

	// --- VERTEX BUFFER VERIFICATION: Read back back_wall vertices from CPU ---
	//     This checks that vertex positions are NOT at the light position.
	{
		const auto& backWallItem = cb.renderItems[0];  // back_wall is first
		NEURUS_LOG("[ShadowRenderDiag] First render item (back_wall):"
		           << " indexCount=" << backWallItem.indexCount
		           << " hasVertexBuffer=" << (backWallItem.vertexBuffer ? "YES" : "NO"));

		// Check pushConstants.model is identity
		const glm::mat4& model = backWallItem.pushConstants.model;
		NEURUS_LOG("  pushConstants.model (should be identity):");
		NEURUS_LOG("    row0: (" << model[0][0] << ", " << model[0][1] << ", "
		           << model[0][2] << ", " << model[0][3] << ")");
		NEURUS_LOG("    row1: (" << model[1][0] << ", " << model[1][1] << ", "
		           << model[1][2] << ", " << model[1][3] << ")");
		NEURUS_LOG("    row2: (" << model[2][0] << ", " << model[2][1] << ", "
		           << model[2][2] << ", " << model[2][3] << ")");
		NEURUS_LOG("    row3: (" << model[3][0] << ", " << model[3][1] << ", "
		           << model[3][2] << ", " << model[3][3] << ")");

		// Read vertex buffer data from GPU staging copy
		vk::DeviceSize vboSize = sizeof(test::CornellBoxVertex) * 12; // back_wall has 12 verts
		vk::BufferCreateInfo stagingCI({}, vboSize, vk::BufferUsageFlagBits::eTransferDst);
		vk::raii::Buffer stagingBuf(*m_device, stagingCI);
		auto stagingReqs = stagingBuf.getMemoryRequirements();
		auto memProps = pd.getMemoryProperties();
		uint32_t stagingType = 0;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((stagingReqs.memoryTypeBits & (1u << i)) &&
			    (memProps.memoryTypes[i].propertyFlags &
			     (vk::MemoryPropertyFlagBits::eHostVisible |
			      vk::MemoryPropertyFlagBits::eHostCoherent)) ==
			        (vk::MemoryPropertyFlagBits::eHostVisible |
			         vk::MemoryPropertyFlagBits::eHostCoherent))
			{ stagingType = i; break; }
		}
		vk::MemoryAllocateInfo stagingAlloc(stagingReqs.size, stagingType);
		vk::raii::DeviceMemory stagingMem(*m_device, stagingAlloc);
		stagingBuf.bindMemory(*stagingMem, 0);

		vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
		                                 m_graphicsQueueFamily);
		vk::raii::CommandPool cmdPool(*m_device, poolCI);
		vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		vk::raii::CommandBuffers cmdBufs(*m_device, allocInfo);
		cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		vk::BufferCopy copyRegion(0, 0, vboSize);
		cmdBufs[0].copyBuffer(backWallItem.vertexBuffer, *stagingBuf, copyRegion);

		vk::MemoryBarrier mb(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
		cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                           vk::PipelineStageFlagBits::eHost,
		                           {}, mb, {}, {});
		cmdBufs[0].end();
		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
		m_queue.submit(submitInfo);
		m_queue.waitIdle();

		auto* verts = static_cast<const test::CornellBoxVertex*>(stagingMem.mapMemory(0, vboSize));
		NEURUS_LOG("  First 3 back_wall vertices (world-space positions):");
		for (int i = 0; i < std::min(3, 12); ++i)
		{
			NEURUS_LOG("    v[" << i << "]: pos=("
			           << verts[i].posX << ", " << verts[i].posY << ", " << verts[i].posZ
			           << ") nrm=(" << verts[i].nrmX << ", " << verts[i].nrmY << ", " << verts[i].nrmZ
			           << ")");
		}
		stagingMem.unmapMemory();

		// Verify that vertex positions are NOT all at the light position
		glm::vec3 lp = cb.light->GetPosition();
		bool allAtLight = true;
		for (int i = 0; i < 12; ++i)
		{
			if (std::abs(verts[i].posX - lp.x) > 0.001f ||
			    std::abs(verts[i].posY - lp.y) > 0.001f ||
			    std::abs(verts[i].posZ - lp.z) > 0.001f)
			{
				allAtLight = false;
				break;
			}
		}
		NEURUS_LOG("  All vertices at light position (" << lp.x << ", " << lp.y << ", " << lp.z
		           << "): " << (allAtLight ? "YES (BUG!)" : "NO (correct)"));
	}

	// Also check face 2 (+Y) which should look upward at the ceiling light
	{
		auto& cubemap = m_shadowDepthPass->ShadowCubemap();
		const uint32_t shadowRes = m_shadowDepthPass->Resolution();
		auto depthResult = ReadCubemapDepthFace(*m_device, pd, m_queue,
		                                         m_graphicsQueueFamily,
		                                         cubemap, 2u, shadowRes);
		NEURUS_LOG("[ShadowRenderDiag] Cubemap face 2 (+Y) depth readback:");
		NEURUS_LOG("  minDepth=" << depthResult.minVal
		           << " maxDepth=" << depthResult.maxVal
		           << " uniqueCount=" << depthResult.uniqueCount
		           << " nonOneCount=" << depthResult.nonOneCount);

		// Print raw bytes of first 4 pixels to confirm float encoding
		if (depthResult.data.size() >= 4)
		{
			const uint8_t* raw = reinterpret_cast<const uint8_t*>(depthResult.data.data());
			NEURUS_LOG("  Raw bytes[0-3] (pixel 0): "
			           << std::hex
			           << "0x" << static_cast<int>(raw[0])
			           << " 0x" << static_cast<int>(raw[1])
			           << " 0x" << static_cast<int>(raw[2])
			           << " 0x" << static_cast<int>(raw[3])
			           << std::dec
			           << " → float=" << depthResult.data[0]);
			NEURUS_LOG("  Raw bytes[4-7] (pixel 1): "
			           << std::hex
			           << "0x" << static_cast<int>(raw[4])
			           << " 0x" << static_cast<int>(raw[5])
			           << " 0x" << static_cast<int>(raw[6])
			           << " 0x" << static_cast<int>(raw[7])
			           << std::dec
			           << " → float=" << depthResult.data[1]);
		}

		// Count how many pixels have depth == 0.0f vs depth == 1.0f
		uint32_t countExactZero = 0, countExactOne = 0;
		for (auto v : depthResult.data)
		{
			if (v == 0.0f) ++countExactZero;
			if (v == 1.0f) ++countExactOne;
		}
		NEURUS_LOG("  exactZero=" << countExactZero
		           << " exactOne=" << countExactOne
		           << " other=" << (depthResult.data.size() - countExactZero - countExactOne));
	}

	// Also verify the G-Buffer position has valid data (quick sanity check)
	{
		auto& posAtt = m_attachmentManager->GetAttachment(AttachmentName::Position);
		// Transition to TRANSFER_SRC for readback
		{
			vk::CommandPoolCreateInfo poolCI(vk::CommandPoolCreateFlagBits::eTransient,
			                                 m_graphicsQueueFamily);
			vk::raii::CommandPool cmdPool(*m_device, poolCI);
			vk::CommandBufferAllocateInfo allocInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
			vk::raii::CommandBuffers cmdBufs(*m_device, allocInfo);
			cmdBufs[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			vk::ImageLayout oldLayout = posAtt.CurrentLayout();
			vk::ImageMemoryBarrier barrier(
				vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
				vk::AccessFlagBits::eTransferRead,
				oldLayout,
				vk::ImageLayout::eTransferSrcOptimal,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				*posAtt.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			cmdBufs[0].pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
			                           vk::PipelineStageFlagBits::eTransfer,
			                           {}, {}, {}, barrier);
			cmdBufs[0].end();
			vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmdBufs[0]));
			m_queue.submit(submitInfo);
			m_queue.waitIdle();
			posAtt.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
		}
		auto posData = posAtt.ReadImageToBuffer(*m_device, pd, m_queue,
		                                         m_graphicsQueueFamily,
		                                         vk::ImageLayout::eTransferSrcOptimal);
		NEURUS_LOG("[ShadowRenderDiag] G-Buffer Position readback:");
		if (posData.size() >= 16)
		{
			const float* fdata = reinterpret_cast<const float*>(posData.data());
			uint32_t countNonZeroW = 0;
			uint32_t pixelCount = static_cast<uint32_t>(posData.size() / 16); // R16G16B16A16_SFLOAT = 8 bytes/pixel
			for (uint32_t i = 0; i < pixelCount; ++i)
			{
				// Read half-floats (2 bytes each for R,G,B,A)
				// For quick check, just look at the A channel which stores w
				const uint16_t* hdata = reinterpret_cast<const uint16_t*>(posData.data());
				uint16_t wHalf = hdata[i * 4 + 3];
				if (wHalf != 0) ++countNonZeroW;
			}
			NEURUS_LOG("  dataSize=" << posData.size()
			           << " pixelCount=" << pixelCount
			           << " nonZeroW=" << countNonZeroW
			           << " (pixels with geometry, w != 0)");
		}
	}

	// --- 5. Capture ShadowIntensity & compare with reference ---
	const std::string refPath = ReferencePath("ShadowIntensity.png");
	const std::string tmpPath = refPath + ".tmp";

	std::filesystem::create_directories(
		std::string("../../../test/render/reference/shadow/"));

	{
		auto& shadowAtt = m_attachmentManager->GetAttachment(
			AttachmentName::ShadowIntensity);
		const bool captured = Screenshot::CaptureAttachment(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			shadowAtt, tmpPath, false);
		ASSERT_TRUE(captured) << "Failed to capture ShadowIntensity";
	}

	const bool refExists = std::ifstream(refPath).good();

	if (!refExists)
	{
		std::rename(tmpPath.c_str(), refPath.c_str());
		GTEST_SKIP() << "Reference image generated. Re-run the test to compare.";
	}
	else
	{
		std::vector<uint8_t> tmpPx, refPx;
		int tmpW, tmpH, tmpC, refW, refH, refC;

		ASSERT_TRUE(LoadPng(tmpPath, tmpPx, tmpW, tmpH, tmpC));
		ASSERT_TRUE(LoadPng(refPath, refPx, refW, refH, refC));
		ASSERT_EQ(tmpW, refW);
		ASSERT_EQ(tmpH, refH);

		const int bad = ComparePixels(tmpPx, refPx, tmpW, tmpH, 2);
		std::remove(tmpPath.c_str());

		EXPECT_EQ(bad, 0) << bad << " pixel(s) differ in ShadowIntensity.";
	}
}
