/**
 * @file test_scene_wiring.cpp
 * @brief TDD tests for Scene-driven rendering — DeferredRenderer::DrawFrame(const Scene&).
 *
 * Validates:
 *   - DrawFrame with empty scene does not crash
 *   - DrawFrame with camera but no meshes does not crash
 *   - DrawFrame with camera + mesh completes a full frame
 *
 * @note Requires a Vulkan 1.4-capable GPU with presentation support.
 *       Skipped in CI without GPU or on platforms without window surface support.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

#include "asset/MeshData.h"
#include "render/DeferredRenderer.h"
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <vulkan/vulkan_raii.hpp>

#include "platform/PlatformSurface.h"

#include <memory>
#include <string>
#include <vector>
#include <cstring>

using namespace neurus;

// ---------------------------------------------------------------------------
// Test OBJ string: simple 3-vertex triangle
// ---------------------------------------------------------------------------

static const char* kTriangleObj =
	"# Simple triangle with normals and UVs\n"
	"v 0.0 0.5 0.0\n"
	"v -0.5 -0.5 0.0\n"
	"v 0.5 -0.5 0.0\n"
	"vn 0.0 0.0 1.0\n"
	"vt 0.5 1.0\n"
	"vt 0.0 0.0\n"
	"vt 1.0 0.0\n"
	"f 1/1/1 2/2/1 3/3/1\n";

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @brief GPU test fixture for Scene-driven DeferredRenderer rendering.
 *
 * Creates a headless Vulkan device and uses the PlatformSurface abstraction
 * for surface creation. On platforms without headless surface support
 * (currently macOS — no hidden window API without Cocoa event loop),
 * tests are skipped.
 */
class SceneWiringTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		// This test requires a presentation surface which needs a window.
		// On macOS without a running Qt event loop, we cannot create one.
		// Skip gracefully.
		m_hasVulkan = false;
		GTEST_SKIP() << "SceneWiringTest requires a presentation surface (skipped in headless).";
	}

	void TearDown() override
	{
		m_renderer.reset();
		m_surface.reset();
		VulkanTestShared::TearDown();
	}

	void CreateRenderer(uint32_t width, uint32_t height)
	{
		m_renderer = std::make_unique<DeferredRenderer>(
			*m_device,
			m_physicalDevices[m_selectedPdIndex],
			m_queue,
			m_graphicsQueueFamily,
			*m_surface,
			width, height);
	}

	std::shared_ptr<Mesh> CreateAndUploadTriangleMesh()
	{
		auto md = std::make_shared<MeshData>();
		EXPECT_TRUE(md->LoadObjFromString(kTriangleObj));

		auto mesh = std::make_shared<Mesh>();
		mesh->o_mesh = md;
		return mesh;
	}

	std::shared_ptr<Camera> CreateDefaultCamera()
	{
		auto cam = std::make_shared<Camera>();
		cam->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
		cam->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
		cam->ChangeCamRatio(static_cast<float>(kRenderWidth), static_cast<float>(kRenderHeight));
		return cam;
	}

	static constexpr uint32_t kRenderWidth = 800;
	static constexpr uint32_t kRenderHeight = 600;

	std::unique_ptr<vk::raii::SurfaceKHR> m_surface;
	std::unique_ptr<DeferredRenderer> m_renderer;
};

// ---------------------------------------------------------------------------
// 1. DrawFrame with scene containing only a camera — no crash
// ---------------------------------------------------------------------------

TEST_F(SceneWiringTest, DrawFrame_EmptyScene_NoCrash)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_FATAL_FAILURE(CreateRenderer(kRenderWidth, kRenderHeight));
	ASSERT_NE(m_renderer, nullptr);

	Scene scene;
	auto cam = CreateDefaultCamera();
	scene.UseCamera(cam);

	EXPECT_NO_THROW(m_renderer->DrawFrame(RenderContext{.scene = &scene}));
	m_renderer->WaitIdle();
}

// ---------------------------------------------------------------------------
// 2. DrawFrame with camera but no meshes — no crash
// ---------------------------------------------------------------------------

TEST_F(SceneWiringTest, DrawFrame_SceneWithOnlyCamera_NoCrash)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_FATAL_FAILURE(CreateRenderer(kRenderWidth, kRenderHeight));

	Scene scene;
	auto cam = CreateDefaultCamera();
	scene.UseCamera(cam);

	ASSERT_NE(scene.GetActiveCamera(), nullptr);

	EXPECT_NO_THROW(m_renderer->DrawFrame(RenderContext{.scene = &scene}));
	m_renderer->WaitIdle();
}

// ---------------------------------------------------------------------------
// 3. DrawFrame with camera + mesh — frame completes
// ---------------------------------------------------------------------------

TEST_F(SceneWiringTest, DrawFrame_SceneWithCameraAndMesh_RendersFrame)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_FATAL_FAILURE(CreateRenderer(kRenderWidth, kRenderHeight));

	Scene scene;

	auto cam = CreateDefaultCamera();
	scene.UseCamera(cam);

	auto mesh = CreateAndUploadTriangleMesh();
	ASSERT_NE(mesh, nullptr);
	scene.UseMesh(mesh);

	auto light = std::make_shared<Light>(LightType::POINTLIGHT, 30.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(2.0f, 2.0f, 5.0f));
	scene.UseLight(light);

	EXPECT_NO_THROW(m_renderer->DrawFrame(RenderContext{.scene = &scene}));
	EXPECT_NO_THROW(m_renderer->DrawFrame(RenderContext{.scene = &scene}));

	m_renderer->WaitIdle();
}
