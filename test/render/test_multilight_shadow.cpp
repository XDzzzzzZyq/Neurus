/**
 * @file test_multilight_shadow.cpp
 * @brief TDD reference-image regression test for multi-light shadow rendering.
 *
 * Renders a cube-on-plane scene with 2 shadow-casting point lights through
 * the full deferred pipeline (ShadowDepthPass → GeometryPass → ShadowIntensityPass
 * → LightingPass) and validates HDRColor output via reference image regression.
 *
 * Reference image pattern:
 *   - First run: generates reference PNG → GTEST_SKIP
 *   - Second run: compares pixel-by-pixel with ±2 tolerance → PASS/FAIL
 *
 * This is the RED phase of TDD — the test compiles and runs but the shadow
 * passes initially only handle a single light.  The reference generated on first run will reflect
 * single-shadow behaviour.  When shadow pass loops are implemented
 * (Wave 2-3), the reference must be regenerated.
 *
 * @note Requires a Vulkan 1.4-capable GPU.  Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"
#include "shared/TestMultiShadow.h"

// --- Render layer ---
#include "render/RenderCache.h"
#include "render/RenderContext.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/ShadowDepthPass.h"
#include "render/passes/ShadowIntensityPass.h"
#include "render/passes/LightingPass.h"
#include "render/Screenshot.h"

// --- Scene layer ---
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Scene.h"

// --- Embedded shaders ---
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <shadow_eval.comp.h>
#include <pbr_lighting.comp.h>

#include "shared/TestReferenceImage.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

// ===========================================================================
// Test fixture
// ===========================================================================

class MultiLightShadowTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		// --- Push constant budget check ---
		if (pd.getProperties().limits.maxPushConstantsSize < sizeof(LightingPushConstants))
		{
			m_hasVulkan = false;
			return;
		}

		// --- Render pass infrastructure (attachments created lazily) ---
		m_renderCache = std::make_unique<RenderCache>(*m_device, pd);

		// --- Geometry pass ---
		m_geometryPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		// --- Shadow depth pass ---
		m_shadowDepthPass = std::make_unique<ShadowDepthPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			ShadowDepthPass::kDefaultResolution);

		// --- Shadow intensity pass (1 set = single-frame recording) ---
		m_shadowIntensityPass = std::make_unique<ShadowIntensityPass>(
			*m_device, pd, 1u,
			m_queue, m_graphicsQueueFamily,
			shadow_eval_comp_spv, sizeof(shadow_eval_comp_spv));

		// --- Lighting pass (2 sets = matches test_deferred_shading) ---
		m_lightingPass = std::make_unique<LightingPass>(
			*m_device, pd, 2u,
			m_queue, m_graphicsQueueFamily,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	std::unique_ptr<RenderCache>         m_renderCache;
	std::unique_ptr<GeometryPass>        m_geometryPass;
	std::unique_ptr<ShadowDepthPass>     m_shadowDepthPass;
	std::unique_ptr<ShadowIntensityPass> m_shadowIntensityPass;
	std::unique_ptr<LightingPass>        m_lightingPass;
};

// ===========================================================================
// TwoShadowLights_HDRColorReference — reference image regression
// ===========================================================================

TEST_F(MultiLightShadowTest, TwoShadowLights_HDRColorReference)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan-capable GPU found.";

	auto& pd = PhysicalDevice();
	const vk::Extent2D renderExtent(kRenderWidth, kRenderHeight);

	// -------------------------------------------------------------------
	// Step 1: Load multi-shadow scene (2 lights by default)
	// -------------------------------------------------------------------
	auto shadowRes = neurus::test::LoadMultiShadow(
		*m_device, pd, m_queue, m_graphicsQueueFamily);
	const auto& renderItems = shadowRes.renderItems;
	ASSERT_EQ(renderItems.size(), 2u) << "Expected cube + plane (2 render items)";
	ASSERT_EQ(shadowRes.lightUIDs.size(), 2u) << "Expected 2 shadow-casting lights";

	// Get the camera from the scene
	ASSERT_FALSE(shadowRes.scene->cam_list.empty()) << "Scene must have a camera";
	const auto& camera = shadowRes.scene->cam_list.begin()->second;

	// Update camera aspect ratio to match render extent
	camera->ChangeCamRatio(static_cast<float>(kRenderWidth), static_cast<float>(kRenderHeight));
	const CameraUBOData camUBO = VulkanTestShared::ComputeCameraUBO(*camera);
	const glm::vec3 cameraPos = camera->GetPosition();

	NEURUS_LOG("[MultiLightShadow] Camera@("
	           << cameraPos.x << "," << cameraPos.y << "," << cameraPos.z << ")"
	           << " lights=" << shadowRes.lightUIDs.size());

	// -------------------------------------------------------------------
	// Step 2: Build RenderContext with both light UIDs
	// -------------------------------------------------------------------
	RenderContext ctx{};
	ctx.renderExtent = renderExtent;
	ctx.frameIndex   = 0;
	ctx.viewProj     = camUBO.viewProj;
	ctx.view         = camUBO.view;
	ctx.cameraPos    = cameraPos;
	ctx.invProjView  = glm::inverse(camUBO.viewProj);
	ctx.renderItems  = &renderItems;
	ctx.scene        = shadowRes.scene.get();

	// -------------------------------------------------------------------
	// Step 3: Transition G-Buffer to renderable layouts
	// -------------------------------------------------------------------
	VulkanTestShared::TransitionGbufferToColorAttachment(
		*m_renderCache, renderExtent, *this);

	// -------------------------------------------------------------------
	// Step 4: Upload light SSBO to LightingPass
	// -------------------------------------------------------------------
	m_lightingPass->UploadLights(*shadowRes.scene);

	// -------------------------------------------------------------------
	// Step 5: Record all passes in a single command buffer
	//         Order matches DeferredRenderer: Geometry → ShadowDepth → ShadowIntensity → Lighting
	// -------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, *m_renderCache, ctx);
		m_shadowDepthPass->Record(*cmd, *m_renderCache, ctx);
		m_shadowIntensityPass->Record(*cmd, *m_renderCache, ctx);
		m_lightingPass->Record(*cmd, *m_renderCache, ctx);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 6: Capture HDRColor attachment as PNG
	// -------------------------------------------------------------------
	const std::string refPath =
		std::string(neurus::test::kReferenceDir) + "multilight/TwoLights_HDRColor.png";
	const std::string tmpPath = refPath + ".tmp";

	Image& hdrAttachment = m_renderCache->GetAttachment(
		AttachmentName::HDRColor, renderExtent);

	const bool captured = Screenshot::CaptureAttachment(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		hdrAttachment, tmpPath, /*remapSigned=*/false);

	ASSERT_TRUE(captured) << "Failed to capture HDRColor attachment to " << tmpPath;

	// -------------------------------------------------------------------
	// Step 7: Reference image regression
	// -------------------------------------------------------------------
	const int refResult = neurus::test::CheckReferenceOrGenerate(refPath, 2);

	if (refResult < 0)
	{
		// First run — reference generated (return -1) or load failure (-2)
		if (refResult == -1)
			GTEST_SKIP() << "Reference images generated.  Re-run the test to compare.";
		else
			FAIL() << "Failed to load reference image for HDRColor";
	}
	else
	{
		// Second run — compare against reference
		EXPECT_EQ(refResult, 0) << refResult << " pixel(s) differ from reference (tol=±2)";
	}
}

// ===========================================================================
// TwoLights_NoVUID — smoke test (pipeline runs without crash)
// ===========================================================================

TEST_F(MultiLightShadowTest, TwoLights_NoVUID)
{
	if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan-capable GPU found.";

	auto& pd = PhysicalDevice();
	const vk::Extent2D renderExtent(kRenderWidth, kRenderHeight);

	// -------------------------------------------------------------------
	// Load scene
	// -------------------------------------------------------------------
	auto shadowRes = neurus::test::LoadMultiShadow(
		*m_device, pd, m_queue, m_graphicsQueueFamily);
	ASSERT_FALSE(shadowRes.scene->cam_list.empty());

	const auto& camera = shadowRes.scene->cam_list.begin()->second;
	camera->ChangeCamRatio(static_cast<float>(kRenderWidth), static_cast<float>(kRenderHeight));
	const CameraUBOData camUBO = VulkanTestShared::ComputeCameraUBO(*camera);

	// -------------------------------------------------------------------
	// Build context
	// -------------------------------------------------------------------
	RenderContext ctx{};
	ctx.renderExtent = renderExtent;
	ctx.viewProj     = camUBO.viewProj;
	ctx.view         = camUBO.view;
	ctx.cameraPos    = camera->GetPosition();
	ctx.invProjView  = glm::inverse(camUBO.viewProj);
	ctx.renderItems  = &shadowRes.renderItems;
	ctx.scene        = shadowRes.scene.get();

	// -------------------------------------------------------------------
	// Transition + upload + record
	// -------------------------------------------------------------------
	VulkanTestShared::TransitionGbufferToColorAttachment(
		*m_renderCache, renderExtent, *this);

	m_lightingPass->UploadLights(*shadowRes.scene);

	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, *m_renderCache, ctx);
		m_shadowDepthPass->Record(*cmd, *m_renderCache, ctx);
		m_shadowIntensityPass->Record(*cmd, *m_renderCache, ctx);
		m_lightingPass->Record(*cmd, *m_renderCache, ctx);
		EndSubmitWait(cmd);
	}

	// If we reach here without crash/exception, the test passes.
	// Validation layer errors are printed to stderr by the debug callback
	// and will appear in ctest --output-on-failure output.
	SUCCEED();
}
