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
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <gbuffer.vert.h>
#include <gbuffer.frag.h>

#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

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
