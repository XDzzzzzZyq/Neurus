/**
 * @file test_ssao.cpp
 * @brief Reference-image regression test for the SSAO pass.
 *
 * Renders the Cornell Box scene through geometry pass and SSAO pass at
 * 256×256, then captures the SSAO attachment as a PNG.
 * On first run the reference image does not exist — the test generates it
 * automatically and reports SKIPPED.  Subsequent runs compare pixel-wise
 * and FAIL on any pixel difference exceeding the allowed tolerance.
 *
 * @note Requires a Vulkan 1.4-capable GPU.  Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"
#include "shared/TestCornellBox.h"

// --- Render layer ---
#include "render/passes/RenderCache.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/RenderContext.h"
#include "render/passes/RenderPassManager.h"
#include "render/passes/SSAOPass.h"
#include "render/Screenshot.h"

// --- Embedded shaders ---
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <ssao.comp.h>

#include "shared/TestReferenceImage.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class SSAOTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		// --- Render pass infrastructure (attachments created lazily) ---
		m_renderCache = std::make_unique<RenderCache>(*m_device, pd);
		m_renderPassManager = std::make_unique<RenderPassManager>();

		// --- Geometry pass ---
		m_geometryPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			*m_renderPassManager,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		// --- SSAO pass ---
		m_ssaoPass = std::make_unique<SSAOPass>(
			*m_device, pd,
			1u,   // one descriptor set for single-frame test
			m_queue, m_graphicsQueueFamily,
			ssao_comp_spv, sizeof(ssao_comp_spv));
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	// --- Render pass infrastructure ---
	std::unique_ptr<RenderCache>  m_renderCache;
	std::unique_ptr<RenderPassManager>  m_renderPassManager;
	std::unique_ptr<GeometryPass>       m_geometryPass;
	std::unique_ptr<SSAOPass>           m_ssaoPass;
};

// ===========================================================================
// SSAO Reference Image Regression Test
// ===========================================================================

TEST_F(SSAOTest, SSAOAttachment_MatchesReferenceImage)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// -------------------------------------------------------------------
	// -------------------------------------------------------------------
	// Step 1: Load Cornell Box scene
	// -------------------------------------------------------------------
	auto cb = test::LoadCornellBox(*m_device, pd, m_queue, m_graphicsQueueFamily);
	ASSERT_GT(cb.renderItems.size(), 0u) << "No meshes loaded for Cornell Box";

	// Adjust camera aspect ratio to match the render target
	cb.camera->ChangeCamRatio(
		static_cast<float>(kRenderWidth),
		static_cast<float>(kRenderHeight));

	const CameraUBOData camUBO = VulkanTestShared::ComputeCameraUBO(*cb.camera);

	// -------------------------------------------------------------------
	// Step 2: Transition G-Buffer & build RenderContext
	// -------------------------------------------------------------------
	VulkanTestShared::TransitionGbufferToColorAttachment(*m_renderCache, {kRenderWidth, kRenderHeight}, *this);

	RenderContext ctx{
		.renderExtent = {kRenderWidth, kRenderHeight},
		.viewProj = camUBO.viewProj,
		.view = camUBO.view,
		.cameraPos = cb.camera->GetPosition(),
		.renderItems = &cb.renderItems,
	};

	// --- Record geometry pass ---
	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, *m_renderCache, ctx);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 3: Run SSAO pass
	// -------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();
		m_ssaoPass->Record(*cmd, *m_renderCache, ctx);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 4: Capture SSAO attachment & compare with reference
	// -------------------------------------------------------------------
	const std::string refPath = std::string(neurus::test::kReferenceDir) + "ssao/SSAO.png";
	const std::string tmpPath = refPath + ".tmp";

	Image& ssaoAttachment = m_renderCache->GetAttachment(AttachmentName::SSAO, {kRenderWidth, kRenderHeight});
	const bool captured = Screenshot::CaptureAttachment(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		ssaoAttachment, tmpPath, false);  // not signed, so no remap

	ASSERT_TRUE(captured) << "Failed to capture SSAO attachment";

	const int result = neurus::test::CheckReferenceOrGenerate(refPath, 2);
	if (result < 0)
		GTEST_SKIP() << "Reference image generated. Re-run the test to compare.";
	EXPECT_EQ(result, 0) << result << " pixel(s) differ in SSAO (threshold: 2 per channel).";
}
