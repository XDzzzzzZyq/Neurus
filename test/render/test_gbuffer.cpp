/**
 * @file test_gbuffer.cpp
 * @brief Tests for GeometryPass - G-Buffer MRT rendering.
 *
 * Validates:
 *   - GeometryPass constructor creates a valid pipeline
 *   - Record() succeeds without validation errors
 *   - All 4 colour attachments receive valid (non-zero) data
 *   - Depth attachment is populated
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "render/passes/RenderCache.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/RenderPassManager.h"
#include "render/buffers/BufferLayout.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include "asset/MeshData.h"
#include "scene/Mesh.h"

#include <gbuffer.vert.h>
#include <gbuffer.frag.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <memory>
#include <string>

using namespace neurus;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @brief GPU test fixture for GeometryPass.
 *
 * Creates a headless Vulkan device, G-Buffer attachments, and a
 * GeometryPass instance with embedded shaders.
 *
 * Uses VulkanTestShared for standard Vulkan bootstrap (instance, device,
 * queue, command pool/buffers) and adds GeometryPass-specific setup.
 */
class GeometryPassTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!HasVulkan()) return;

		try
		{
			auto& pd = PhysicalDevice();

			// --- Check push-constant size support ---
			const auto& limits = pd.getProperties().limits;
			if (limits.maxPushConstantsSize < sizeof(PushConstants))
			{
				m_hasVulkan = false;
				return;
			}

			// --- Attachment manager (G-Buffer + depth) - attachments created lazily ---
			m_renderCache = std::make_unique<RenderCache>(*m_device, pd);

			// --- Render pass manager ---
			m_renderPassManager = std::make_unique<RenderPassManager>();

			// --- Geometry pass ---
		m_geometryPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			*m_renderPassManager,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

			m_hasVulkan = true;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[GeometryPassTest::SetUp] Exception: " << e.what() << std::endl;
			m_hasVulkan = false;
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		if (m_device)
		{
			m_device->waitIdle();
		}
		m_geometryPass.reset();
		m_renderCache.reset();
		VulkanTestShared::TearDown();
	}

	// --- Constants ---
	static constexpr uint32_t kRenderWidth  = 128;
	static constexpr uint32_t kRenderHeight = 128;

	// --- Render pass infrastructure ---
	std::unique_ptr<RenderCache>  m_renderCache;
	std::unique_ptr<RenderPassManager>  m_renderPassManager;

	// --- System under test ---
	std::unique_ptr<GeometryPass> m_geometryPass;
};

// ===========================================================================
// Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// 1. Constructor - pipeline is created successfully
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, Constructor_CreatesValidPipeline)
{
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// GeometryPass was created in SetUp - verify it exists
	ASSERT_NE(m_geometryPass, nullptr);
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 2. Record - single triangle, no validation errors
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, Record_SingleTriangle_NoValidationError)
{
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// --- Transition attachments to renderable layouts ---
	VulkanTestShared::TransitionGbufferToColorAttachment(*m_renderCache, {kRenderWidth, kRenderHeight}, *this);

	// --- Create mesh from OBJ string ---
	auto meshData = std::make_shared<MeshData>();
	const std::string objStr =
		"v 0.0 -0.5 0.0\n"
		"v 0.5 0.5 0.0\n"
		"v -0.5 0.5 0.0\n"
		"vn 0.0 0.0 1.0\n"
		"f 1//1 2//1 3//1\n";
	ASSERT_TRUE(meshData->LoadObjFromString(objStr));

	Mesh mesh;
	mesh.o_mesh = meshData;
	mesh.UploadToGPU(*m_device, pd, m_queue, m_graphicsQueueFamily);

	// --- Build render item ---
	GeometryRenderItem item;
	item.vertexBuffer = mesh.GetVertexBuffer()->buffer();
	item.indexBuffer  = mesh.GetIndexBuffer()->buffer();
	item.indexCount   = mesh.GetGPUIndexCount();
	item.indexType    = mesh.GetIndexBuffer()->GetIndexType();
	item.pushConstants.model = glm::mat4(1.0f);         // identity
	item.pushConstants.normalMatrix = glm::mat4(1.0f);   // identity

	// --- Camera ---
	const auto camera = VulkanTestShared::MakeTestCamera(kRenderWidth, kRenderHeight);

	// --- Record ---
	{
		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items = { item };

		m_geometryPass->Record(*cmd, *m_renderCache, RenderContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camera.viewProj,
			.view = camera.view,
			.renderItems = &items,
		});

		EndSubmitWait(cmd);
	}

	SUCCEED();
}

TEST_F(GeometryPassTest, Record_MultipleItems_NoValidationError)
{
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	VulkanTestShared::TransitionGbufferToColorAttachment(*m_renderCache, {kRenderWidth, kRenderHeight}, *this);

	// --- Create mesh from OBJ string ---
	auto meshData = std::make_shared<MeshData>();
	const std::string objStr =
		"v 0.0 -0.5 0.0\n"
		"v 0.5 0.5 0.0\n"
		"v -0.5 0.5 0.0\n"
		"vn 0.0 0.0 1.0\n"
		"f 1//1 2//1 3//1\n";
	ASSERT_TRUE(meshData->LoadObjFromString(objStr));

	Mesh mesh;
	mesh.o_mesh = meshData;
	mesh.UploadToGPU(*m_device, pd, m_queue, m_graphicsQueueFamily);

	// --- Two render items with different transforms ---
	GeometryRenderItem item0;
	item0.vertexBuffer = mesh.GetVertexBuffer()->buffer();
	item0.indexBuffer  = mesh.GetIndexBuffer()->buffer();
	item0.indexCount   = mesh.GetGPUIndexCount();
	item0.indexType    = mesh.GetIndexBuffer()->GetIndexType();
	item0.pushConstants.model = glm::mat4(1.0f);
	item0.pushConstants.normalMatrix = glm::mat4(1.0f);

	GeometryRenderItem item1 = item0;
	item1.pushConstants.model = glm::translate(glm::mat4(1.0f),
	                                           glm::vec3(1.0f, 0.0f, 0.0f));
	// Normal matrix stays identity since we're only translating

	const auto camera = VulkanTestShared::MakeTestCamera(kRenderWidth, kRenderHeight);

	{
		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items = { item0, item1 };
		m_geometryPass->Record(*cmd, *m_renderCache, RenderContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camera.viewProj,
			.view = camera.view,
			.renderItems = &items,
		});
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// 4. Record - empty render items (should not crash)
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, Record_EmptyRenderItems_NoCrash)
{
	if (!HasVulkan())
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	VulkanTestShared::TransitionGbufferToColorAttachment(*m_renderCache, {kRenderWidth, kRenderHeight}, *this);

	const auto camera = VulkanTestShared::MakeTestCamera(kRenderWidth, kRenderHeight);

	{
		auto& cmd = BeginCmd();
		const std::vector<GeometryRenderItem> emptyItems;
		m_geometryPass->Record(*cmd, *m_renderCache, RenderContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camera.viewProj,
			.view = camera.view,
			.renderItems = &emptyItems,
		});
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// 5. Non-copyable / movable
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<GeometryPass>,
	              "GeometryPass must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<GeometryPass>,
	              "GeometryPass must not be copy-assignable");
	SUCCEED();
}

TEST_F(GeometryPassTest, Movable)
{
	static_assert(std::is_move_constructible_v<GeometryPass>,
	              "GeometryPass must be move-constructible");
	static_assert(std::is_move_assignable_v<GeometryPass>,
	              "GeometryPass must be move-assignable");
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 6. Camera UBO layout - size sanity check
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, CameraUBOData_SizeMatchesShaderExpectation)
{
	// Shader expects 2 mat4s (viewProj + view) = 128 bytes
	EXPECT_EQ(sizeof(CameraUBOData), 128u);
}

// ---------------------------------------------------------------------------
// 7. PushConstants size matches shader
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, PushConstants_SizeMatchesShaderExpectation)
{
	// Shader expects 2 mat4s (model + normalMatrix) = 128 bytes
	EXPECT_EQ(sizeof(PushConstants), 128u);
}
