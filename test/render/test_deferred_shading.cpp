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

// --- Render layer ---
#include "render/passes/RenderCache.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/LightingPass.h"
#include "render/passes/RenderContext.h"
#include "render/passes/RenderPassManager.h"
#include "render/Material.h"
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
		m_renderPassManager = std::make_unique<RenderPassManager>();

		// --- Geometry pass ---
		m_geometryPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			*m_renderPassManager,
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
	std::unique_ptr<RenderPassManager>  m_renderPassManager;
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
	// Step 1: Load sphere OBJ
	// -------------------------------------------------------------------
	std::string objPath = ResolveAssetPath("res/obj/sphere.obj");

	auto meshData = std::make_shared<MeshData>();
	const bool loaded = meshData->LoadObj(objPath);
	ASSERT_TRUE(loaded) << "Failed to load OBJ: " << objPath;

	const auto& rawMesh = meshData->GetMeshData();
	ASSERT_GT(rawMesh.dataArray.size() / 14, 0u);
	ASSERT_GT(rawMesh.indexArray.size(), 0u);

	// -------------------------------------------------------------------
	// Step 2: Create camera (same as default scene: pos (0,2,5), target origin)
	// -------------------------------------------------------------------
	auto camera = std::make_shared<Camera>(
		static_cast<float>(kRenderWidth),
		static_cast<float>(kRenderHeight),
		60.0f, 0.1f, 100.0f);
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	const CameraUBOData camUBO = VulkanTestShared::ComputeCameraUBO(*camera);

	// -------------------------------------------------------------------
	// Step 3: Create point light
	// -------------------------------------------------------------------
	auto light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(2.0f, 2.0f, 2.0f));
	light->light_radius = 10.0f;

	// -------------------------------------------------------------------
	// Step 4: Build mesh + material
	// -------------------------------------------------------------------
	auto material = std::make_shared<Material>();
	material->SetMatParam(Material::MAT_METAL, 0.0f);
	material->SetMatParam(Material::MAT_ROUGH, 0.5f);
	material->SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f, 1.0f, 1.0f));

	auto mesh = std::make_shared<Mesh>();
	mesh->o_mesh = meshData;
	mesh->o_material = material;

	// -------------------------------------------------------------------
	// Step 5: Upload mesh to GPU via Mesh::UploadToGPU()
	// -------------------------------------------------------------------
	mesh->UploadToGPU(*m_device, pd, m_queue, m_graphicsQueueFamily);

	GeometryRenderItem renderItem;
	renderItem.vertexBuffer = mesh->GetVertexBuffer()->buffer();
	renderItem.indexBuffer  = mesh->GetIndexBuffer()->buffer();
	renderItem.indexCount   = mesh->GetGPUIndexCount();
	renderItem.indexType    = mesh->GetIndexBuffer()->GetIndexType();
	// Scale by 0.25 to match original vertex-scaling behaviour (positions
	// were multiplied by 0.25 in the manual conversion that this replace).
	renderItem.pushConstants.model = glm::scale(glm::mat4(1.0f), glm::vec3(0.25f));
	renderItem.pushConstants.normalMatrix = glm::mat4(1.0f);

	// -------------------------------------------------------------------
	// Step 7: Transition G-Buffer attachments & record geometry pass
	// -------------------------------------------------------------------
	VulkanTestShared::TransitionGbufferToColorAttachment(*m_renderCache, {kRenderWidth, kRenderHeight}, *this);

	{
		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items = { renderItem };
		m_geometryPass->Record(*cmd, *m_renderCache, RenderContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camUBO.viewProj,
			.view = camUBO.view,
			.renderItems = &items,
		});
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 8: Upload light SSBO & record lighting pass
	// -------------------------------------------------------------------
	{
		Scene scene;
		scene.UseLight(light);
		m_lightingPass->UploadLights(scene);
	}

	{
		auto& cmd = BeginCmd();
		m_lightingPass->Record(*cmd, *m_renderCache, RenderContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.frameIndex = 0,
			.view = camUBO.view,
			.cameraPos = camera->GetPosition(),
			.invProjView = glm::inverse(camUBO.viewProj),
		});
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
