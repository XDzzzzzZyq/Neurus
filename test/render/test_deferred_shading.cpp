/**
 * @file test_deferred_shading.cpp
 * @brief Reference-image regression test for the deferred shading pipeline (G-Buffer + HDR passes).
 *
 * Renders a known scene (sphere OBJ + point light) at 256×256 through the
 * full deferred pipeline and captures Position, Normal, Albedo, MetallicRoughness
 * and HDRColor attachments as PNGs.  Each PNG is compared pixel‑wise against
 * a ground‑truth reference image stored in test/render/reference/.
 *
 * On first run the reference images DO NOT exist — the test generates them
 * automatically and reports SKIPPED.  Subsequent runs compare and FAIL on
 * any pixel difference exceeding the allowed tolerance.
 *
 * @note Requires a Vulkan 1.4‑capable GPU.  Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"
#include "shared/TestDeferredScene.h"

// --- Render layer ---
#include "render/RenderCache.h"
#include "render/RenderContext.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/LightingPass.h"
#include "scene/Material.h"
#include "render/Screenshot.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"
#include "render/Texture.h"

// --- Data layer ---
#include "asset/MeshData.h"

// --- Scene layer ---
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

// --- Embedded shaders ---
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <pbr_lighting.comp.h>

#include "shared/TestReferenceImage.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Attachment list for reference comparison
// ---------------------------------------------------------------------------
static constexpr AttachmentName kReferenceAttachments[] = {
	AttachmentName::Position,
	AttachmentName::Normal,
	AttachmentName::Albedo,
	AttachmentName::MetallicRoughness,
	AttachmentName::HDRColor,
};

static constexpr int kReferenceAttachmentCount = static_cast<int>(std::size(kReferenceAttachments));

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class DeferredShadingTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

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

		// --- Lighting pass ---
		m_lightingPass = std::make_unique<LightingPass>(
			*m_device, pd,
			2u,
			m_queue, m_graphicsQueueFamily,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	// --- Render pass infrastructure ---
	std::unique_ptr<RenderCache>  m_renderCache;
	std::unique_ptr<GeometryPass>       m_geometryPass;
	std::unique_ptr<LightingPass>       m_lightingPass;
};

// ===========================================================================
// Deferred Shading Reference Image Regression Test
// ===========================================================================

TEST_F(DeferredShadingTest, GbufferAttachments_MatchReferenceImages)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

 	// -------------------------------------------------------------------
	// Step 1: Load sphere scene (shared helper)
	// -------------------------------------------------------------------
	auto scene = neurus::test::BuildDeferredScene(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		ResolveAssetPath("res/obj/sphere.obj"));

	const CameraUBOData camUBO = VulkanTestShared::ComputeCameraUBO(*scene.camera);
	GeometryRenderItem renderItem = scene.renderItem;
	std::vector<GeometryRenderItem> items = { renderItem };

	// -------------------------------------------------------------------
	// Step 7: Transition G-Buffer attachments & build RenderContext
	// -------------------------------------------------------------------
	VulkanTestShared::TransitionGbufferToColorAttachment(*m_renderCache, {kRenderWidth, kRenderHeight}, *this);

	RenderContext ctx{
		.renderExtent = {kRenderWidth, kRenderHeight},
		.frameIndex = 0,
		.viewProj = camUBO.viewProj,
		.view = camUBO.view,
		.cameraPos = scene.camera->GetPosition(),
		.invProjView = glm::inverse(camUBO.viewProj),
		.renderItems = &items,
	};

	// --- Record geometry pass ---
	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, *m_renderCache, ctx);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 8: Upload light SSBO & record lighting pass
	// -------------------------------------------------------------------
	{
		Scene testScene;
		testScene.UseLight(scene.light);
		m_lightingPass->UploadLights(testScene);
	}

	{
		auto& cmd = BeginCmd();
		m_lightingPass->Record(*cmd, *m_renderCache, ctx);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 9: Capture attachment screenshots & compare with reference images
	// -------------------------------------------------------------------
	int totalBadPixels = 0;
	bool anyGenerated = false;

	for (int i = 0; i < kReferenceAttachmentCount; ++i)
	{
		const AttachmentName name = kReferenceAttachments[i];
		const bool isNormal = (name == AttachmentName::Normal);

		const std::string refPath = std::string(neurus::test::kReferenceDir)
			+ "deferred/" + AttachmentNameToString(name) + ".png";
		const std::string tmpPath = refPath + ".tmp";

		Image& attachment = m_renderCache->GetAttachment(name, {kRenderWidth, kRenderHeight});
		const bool captured = Screenshot::CaptureAttachment(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			attachment, tmpPath, isNormal);

		ASSERT_TRUE(captured) << "Failed to capture attachment: "
		                      << AttachmentNameToString(name);

		const int result = neurus::test::CheckReferenceOrGenerate(refPath, 2);
		if (result < 0)
		{
			if (result == -1)
				anyGenerated = true;
			else
				FAIL() << "Failed to load reference image for " << AttachmentNameToString(name);
		}
		else
		{
			totalBadPixels += result;
		}
	}

	if (anyGenerated)
	{
		GTEST_SKIP() << "Reference images generated.  Re-run the test to compare.";
	}
	EXPECT_EQ(totalBadPixels, 0)
		<< totalBadPixels << " pixel(s) differ in reference comparison.";
}
