/**
 * @file test_lighting.cpp
 * @brief Tests for LightingPass - PBR Cook-Torrance GGX compute shader.
 *
 * Validates:
 *   - LightingPass constructor creates a valid pipeline
 *   - Single point light dispatch produces non-zero HDR output
 *   - G-Buffer reading and HDR writing work correctly
 *   - Push constants (light count, camera pos, view matrix) are consumed
 *   - PointLightGpu SSBO layout matches shader expectation
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "render/passes/RenderCache.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/LightingPass.h"
#include "render/passes/RenderContext.h"
#include "render/buffers/GPUBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include "asset/MeshData.h"

#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <pbr_lighting.comp.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <memory>

using namespace neurus;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @brief GPU test fixture for LightingPass.
 *
 * Creates a headless Vulkan device, G-Buffer attachments, renders a triangle
 * via GeometryPass, then dispatches the PBR lighting compute shader and
 * reads back the HDR colour output.
 */
class LightingPassTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		// --- Check push-constant size support ---
		const auto& limits = pd.getProperties().limits;
		if (limits.maxPushConstantsSize < sizeof(LightingPushConstants))
		{
			m_hasVulkan = false;
			return;
		}

		// --- Attachment manager (G-Buffer + HDR color + depth) - attachments created lazily ---
		m_renderCache = std::make_unique<RenderCache>(*m_device, pd);

		// --- Geometry pass ---
		m_geometryPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		// --- Lighting pass ---
		m_lightingPass = std::make_unique<LightingPass>(
			*m_device, pd,
			2u,                          // numSets = kMaxFramesInFlight
			m_queue, m_graphicsQueueFamily,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
	}

	/**
	 * @brief Renders the test triangle into the G-Buffer.
	 *
	 * Transitions attachments, creates buffers, records geometry pass.
	 * Returns the camera UBO data used for the rendering.
	 */
	CameraUBOData RenderTestTriangle()
	{
		auto& pd = PhysicalDevice();

		// Transition GBuffer → renderable
		VulkanTestShared::TransitionGbufferToColorAttachment(*m_renderCache, {kRenderWidth, kRenderHeight}, *this);

		// Build test triangle mesh from inline OBJ data → Mesh → UploadToGPU
		const std::string triObj =
			"o TestTriangle\n"
			"v 0.0 -0.5 0.0\n"
			"v 0.5 0.5 0.0\n"
			"v -0.5 0.5 0.0\n"
			"vn 0.0 0.0 1.0\n"
			"vn 0.0 0.0 1.0\n"
			"vn 0.0 0.0 1.0\n"
			"vt 0.5 1.0\n"
			"vt 1.0 0.0\n"
			"vt 0.0 0.0\n"
			"f 1/1/1 2/2/2 3/3/3\n";

		auto meshData = std::make_shared<MeshData>();
		meshData->LoadObjFromString(triObj);
		Mesh mesh;
		mesh.o_mesh = meshData;
		mesh.UploadToGPU(*m_device, pd, m_queue, m_graphicsQueueFamily);

		GeometryRenderItem item;
		item.vertexBuffer = mesh.GetVertexBuffer()->buffer();
		item.indexBuffer  = mesh.GetIndexBuffer()->buffer();
		item.indexCount   = mesh.GetGPUIndexCount();
		item.indexType    = mesh.GetIndexBuffer()->GetIndexType();
		item.pushConstants.model = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		const auto camera = VulkanTestShared::MakeTestCamera(kRenderWidth, kRenderHeight);

		{
			auto& cmd = BeginCmd();
			std::vector<GeometryRenderItem> items = {item};
			m_geometryPass->Record(*cmd, *m_renderCache, RenderContext{
				.renderExtent = {kRenderWidth, kRenderHeight},
				.viewProj = camera.viewProj,
				.view = camera.view,
				.renderItems = &items,
			});
			EndSubmitWait(cmd);
		}

		return camera;
	}

	/**
	 * @brief Creates a point light SSBO with a single light.
	 */
	std::unique_ptr<GPUBuffer> CreateLightSSBO(const glm::vec3& pos,
	                                              float power = 50.0f,
	                                              const glm::vec3& color = glm::vec3(1.0f))
	{
		const auto& pd = m_physicalDevices[m_selectedPdIndex];

		PointLightGpu light = {};
		light.colorR = color.r;
		light.colorG = color.g;
		light.colorB = color.b;
		light.posX   = pos.x;
		light.posY   = pos.y;
		light.posZ   = pos.z;
		light.power  = power;
		light.radius = 0.05f;

		auto ssbo = std::make_unique<GPUBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			sizeof(PointLightGpu),
			vk::BufferUsageFlagBits::eStorageBuffer);

		ssbo->Upload(&light, sizeof(PointLightGpu));
		return ssbo;
	}

	/**
	 * @brief Reads back HDR colour output into a float array.
	 *
	 * Assumes RGBA16F format (8 bytes per pixel). Converts half-floats to
	 * single-precision floats.
	 */
	// --- Constants ---
	static constexpr uint32_t kRenderWidth  = 128;
	static constexpr uint32_t kRenderHeight = 128;

	// --- Render pass infrastructure ---
	std::unique_ptr<RenderCache>  m_renderCache;

	// --- Systems under test ---
	std::unique_ptr<GeometryPass>  m_geometryPass;
	std::unique_ptr<LightingPass>  m_lightingPass;
};

// ===========================================================================
// Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// 1. Constructor - pipeline is created successfully
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, Constructor_CreatesValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NE(m_lightingPass, nullptr);
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 2. PointLightGpu - size matches shader expectation (std140, 48 bytes)
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, PointLightGpu_SizeIs48Bytes)
{
	EXPECT_EQ(sizeof(PointLightGpu), 48u);
}

// ---------------------------------------------------------------------------
// 3. LightingPushConstants - size matches shader expectation (100 bytes)
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, PushConstants_SizeIs176Bytes)
{
	EXPECT_EQ(sizeof(LightingPushConstants), 176u);
}

// ---------------------------------------------------------------------------
// 4. Non-copyable / movable
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<LightingPass>,
	              "LightingPass must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<LightingPass>,
	              "LightingPass must not be copy-assignable");
	SUCCEED();
}

TEST_F(LightingPassTest, Movable)
{
	static_assert(std::is_move_constructible_v<LightingPass>,
	              "LightingPass must be move-constructible");
	static_assert(std::is_move_assignable_v<LightingPass>,
	              "LightingPass must be move-assignable");
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 5. Single point light - dispatch produces non-zero HDR output
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, SinglePointLight_ProducesNonZeroOutput)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// --- Step 1: Render test triangle into G-Buffer ---
	const auto camera = RenderTestTriangle();

	// --- Step 2: Upload point light to LightingPass ---
	{
		Scene scene;
		auto light = std::make_shared<Light>(LightType::POINTLIGHT, 50.0f, glm::vec3(1.0f, 1.0f, 1.0f));
		light->SetPosition(glm::vec3(0.0f, 0.0f, 3.0f));
		scene.UseLight(light);
		m_lightingPass->UploadLights(scene);
	}

	// --- Step 3: Record lighting pass ---
	{
		auto& cmd = BeginCmd();
		const auto testCam = VulkanTestShared::MakeTestCamera(kRenderWidth, kRenderHeight);

		m_lightingPass->Record(*cmd, *m_renderCache, RenderContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.frameIndex = 0,
			.view = testCam.view,
			.cameraPos = glm::vec3(0.0f, 0.0f, 2.0f),
			.invProjView = glm::inverse(testCam.viewProj),
		});

		EndSubmitWait(cmd);
	}

	// --- Step 4: Read back HDR output ---
	std::vector<float> hdrPixels = VulkanTestShared::ReadbackHdrOutput(
		*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily,
		*m_renderCache, kRenderWidth, kRenderHeight);

	// --- Step 5: Verify at least one pixel has non-zero colour ---
	// The triangle covers roughly the center of the framebuffer.
	// Scan all pixels for any non-zero RGB values.
	bool foundNonZero = false;
	for (size_t i = 0; i < hdrPixels.size(); i += 4)
	{
		float r = hdrPixels[i + 0];
		float g = hdrPixels[i + 1];
		float b = hdrPixels[i + 2];

		if (r > 0.01f || g > 0.01f || b > 0.01f)
		{
			foundNonZero = true;
			break;
		}
	}

	EXPECT_TRUE(foundNonZero)
		<< "No non-zero pixel found in HDR output after single point light dispatch. "
		<< "The lighting pass should produce visible illumination on the test triangle.";

	if (!foundNonZero)
	{
		// Dump center pixel for debugging
		const size_t centerIdx = (kRenderHeight / 2 * kRenderWidth + kRenderWidth / 2) * 4;
		ADD_FAILURE()
			<< "Center pixel RGBA: ("
			<< hdrPixels[centerIdx + 0] << ", "
			<< hdrPixels[centerIdx + 1] << ", "
			<< hdrPixels[centerIdx + 2] << ", "
			<< hdrPixels[centerIdx + 3] << ")";
	}
}

// ---------------------------------------------------------------------------
// 6. Zero lights with PARTIALLY_BOUND descriptor — validates that the
//    VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT on the SSBO binding works
//    correctly. When m_lightSSBO is null, the shader must not crash because
//    lightCount=0 prevents any SSBO reads.
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, ZeroLights_PartiallyBoundDescriptor)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// --- Render triangle ---
	const auto camera = RenderTestTriangle();

	// --- Upload zero lights (empty scene — m_lightSSBO becomes null) ---
	{
		Scene emptyScene;
		m_lightingPass->UploadLights(emptyScene);
	}

	// --- Record lighting pass with 0 lights (PARTIALLY_BOUND SSBO) ---
	{
		auto& cmd = BeginCmd();
		const auto testCam = VulkanTestShared::MakeTestCamera(kRenderWidth, kRenderHeight);

		RenderContext ctx{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.frameIndex = 0,
			.view = testCam.view,
			.cameraPos = glm::vec3(0.0f, 0.0f, 2.0f),
			.invProjView = glm::inverse(testCam.viewProj),
		};
		m_lightingPass->Record(*cmd, *m_renderCache, ctx);
		EndSubmitWait(cmd);
	}

	// --- Read back ---
	std::vector<float> hdrPixels = VulkanTestShared::ReadbackHdrOutput(
		*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily,
		*m_renderCache, kRenderWidth, kRenderHeight);

	// --- Verify: the GPU didn't crash, no VUID errors, HDR data is valid ---
	// With zero lights the shader still produces ambient output (~0.03).
	// This test primarily validates that the PARTIALLY_BOUND SSBO binding
	// doesn't cause a crash or validation error when m_lightSSBO is null.
	bool foundLit = false;
	for (size_t i = 0; i < hdrPixels.size(); i += 4)
	{
		float r = hdrPixels[i + 0];
		if (r > 0.001f && r < 0.05f)
		{
			foundLit = true;
			break;
		}
	}

	EXPECT_TRUE(foundLit)
		<< "Zero-light dispatch with PARTIALLY_BOUND SSBO should produce "
		<< "valid output (ambient ~0.03) on the test triangle. "
		<< "If the GPU crashed, the test harness would have caught it.";
}
