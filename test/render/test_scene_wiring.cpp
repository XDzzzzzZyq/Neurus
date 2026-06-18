/**
 * @file test_scene_wiring.cpp
 * @brief TDD tests for Scene-driven rendering — DeferredRenderer::DrawFrame(const Scene&).
 *
 * Validates:
 *   - DrawFrame with empty scene does not crash
 *   - DrawFrame with camera but no meshes does not crash
 *   - DrawFrame with camera + mesh completes a full frame
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 * @note Creates a hidden Win32 window for the presentation surface.
 */

#include <gtest/gtest.h>

#include "TestVulkanFixture.h"

#include "data/GPUResourceCache.h"
#include "data/MeshData.h"
#include "render/DeferredRenderer.h"
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

// Embedded shaders
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <pbr_lighting.comp.h>

#include <vulkan/vulkan_raii.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
 * Creates a headless Vulkan device, a hidden Win32 window + surface,
 * GPUResourceCache, and the shader SPIR-V is embedded via generated headers.
 *
 * Uses VulkanTestFixture for member variables (m_instance, m_physicalDevices,
 * m_device, m_queue, etc.) but overrides SetUp/TearDown entirely because
 * this test needs a device with VK_KHR_swapchain enabled and a Win32 surface.
 */
class SceneWiringTest : public VulkanTestFixture
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Instance (via base class pattern) ---
			vk::ApplicationInfo appInfo("NeurusTest_SceneWiring",
			                            VK_MAKE_VERSION(0, 4, 5),
			                            "NeurusTest_SceneWiring",
			                            VK_MAKE_VERSION(0, 4, 5),
			                            VK_API_VERSION_1_4);
			std::vector<const char*> instanceExts = {
				VK_KHR_SURFACE_EXTENSION_NAME,
				VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
				VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
			};
			vk::InstanceCreateInfo instanceCI({}, &appInfo, {}, instanceExts);
			m_instance = std::make_unique<vk::raii::Instance>(m_context, instanceCI);

			// --- Physical device ---
			m_physicalDevices = vk::raii::PhysicalDevices(*m_instance);
			if (m_physicalDevices.empty())
			{
				m_hasVulkan = false;
				return;
			}

			// Pick discrete GPU if available
			m_selectedPdIndex = 0;
			for (uint32_t i = 0; i < static_cast<uint32_t>(m_physicalDevices.size()); ++i)
			{
				const auto props = m_physicalDevices[i].getProperties();
				if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
				{
					m_selectedPdIndex = i;
					break;
				}
			}
			auto& pd = m_physicalDevices[m_selectedPdIndex];

			// --- Queue family ---
			auto qfProps = pd.getQueueFamilyProperties();
			m_graphicsQueueFamily = UINT32_MAX;
			for (uint32_t i = 0; i < static_cast<uint32_t>(qfProps.size()); ++i)
			{
				if (qfProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
				{
					m_graphicsQueueFamily = i;
					break;
				}
			}
			if (m_graphicsQueueFamily == UINT32_MAX)
			{
				m_hasVulkan = false;
				return;
			}

			// --- Device (must enable VK_KHR_swapchain for DeferredRenderer) ---
			std::vector<const char*> devExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

			// VK_KHR_portability_subset is required when running on portability layers
			// (e.g. MoltenVK compatibility profile). Check if supported and add.
			for (const auto& ext : pd.enumerateDeviceExtensionProperties())
			{
				if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0)
				{
					devExts.push_back("VK_KHR_portability_subset");
					break;
				}
			}

			float prio = 1.0f;
			vk::DeviceQueueCreateInfo qCI({}, m_graphicsQueueFamily, 1, &prio);
			vk::PhysicalDeviceFeatures features;
			vk::DeviceCreateInfo devCI({}, qCI, {}, devExts, &features);
			m_device = std::make_unique<vk::raii::Device>(pd, devCI);
			m_queue = m_device->getQueue(m_graphicsQueueFamily, 0);

			// --- Hidden window + surface ---
			HINSTANCE hInst = GetModuleHandle(nullptr);
			WNDCLASSEX wc = {};
			wc.cbSize = sizeof(WNDCLASSEX);
			wc.lpfnWndProc = DefWindowProc;
			wc.hInstance = hInst;
			wc.lpszClassName = L"NeurusTestSceneWiring";
			RegisterClassEx(&wc);

			m_hwnd = CreateWindowEx(0, L"NeurusTestSceneWiring", L"Test",
			                        WS_POPUP, 0, 0,
			                        static_cast<int>(kRenderWidth),
			                        static_cast<int>(kRenderHeight),
			                        nullptr, nullptr, hInst, nullptr);
			if (!m_hwnd)
			{
				m_hasVulkan = false;
				return;
			}

			vk::Win32SurfaceCreateInfoKHR surfaceCI({}, hInst, m_hwnd);
			m_surface = std::make_unique<vk::raii::SurfaceKHR>(*m_instance, surfaceCI);

			// --- GPUResourceCache ---
			m_resourceCache = std::make_unique<GPUResourceCache>(
				*m_device, pd, m_queue, m_graphicsQueueFamily);

			m_hasVulkan = true;
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		// Destroy DeferredRenderer before device
		m_renderer.reset();
		m_resourceCache.reset();
		m_surface.reset();
		if (m_hwnd)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
		VulkanTestFixture::TearDown();
	}

	/**
	 * @brief Creates the DeferredRenderer with embedded SPIR-V shaders.
	 */
	void CreateRenderer(uint32_t width, uint32_t height)
	{
		m_renderer = std::make_unique<DeferredRenderer>(
			*m_device,
			m_physicalDevices[m_selectedPdIndex],
			m_queue,
			m_graphicsQueueFamily,
			*m_surface,
			width, height,
			*m_resourceCache,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv),
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
	}

	/**
	 * @brief Creates a triangle Mesh and uploads it to the resource cache.
	 */
	std::shared_ptr<Mesh> CreateAndUploadTriangleMesh()
	{
		auto md = std::make_shared<MeshData>();
		EXPECT_TRUE(md->LoadObjFromString(kTriangleObj));

		auto mesh = std::make_shared<Mesh>();
		mesh->o_mesh = md;
		m_resourceCache->UploadMesh(*mesh);
		return mesh;
	}

	/**
	 * @brief Creates a Camera positioned at (0, 2, 5) looking at origin.
	 */
	std::shared_ptr<Camera> CreateDefaultCamera()
	{
		auto cam = std::make_shared<Camera>();
		cam->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
		cam->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
		cam->ChangeCamRatio(static_cast<float>(kRenderWidth), static_cast<float>(kRenderHeight));
		return cam;
	}

	static constexpr uint32_t kRenderWidth = 800;
	static constexpr uint32_t kRenderHeight = 600;

	HWND m_hwnd = nullptr;
	std::unique_ptr<vk::raii::SurfaceKHR> m_surface;

	std::unique_ptr<GPUResourceCache> m_resourceCache;
	std::unique_ptr<DeferredRenderer> m_renderer;
};

// ---------------------------------------------------------------------------
// 1. DrawFrame with empty scene — no crash
// ---------------------------------------------------------------------------

/**
 * @test Constructing DeferredRenderer and calling DrawFrame with an empty scene does not crash.
 */
TEST_F(SceneWiringTest, DrawFrame_EmptyScene_NoCrash)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_FATAL_FAILURE(CreateRenderer(kRenderWidth, kRenderHeight));
	ASSERT_NE(m_renderer, nullptr);

	Scene emptyScene;

	// DrawFrame must not throw or cause a fatal failure
	EXPECT_NO_THROW(m_renderer->DrawFrame(emptyScene));

	// Ensure GPU is idle before teardown
	m_renderer->WaitIdle();
}

// ---------------------------------------------------------------------------
// 2. DrawFrame with camera but no meshes — no crash
// ---------------------------------------------------------------------------

/**
 * @test DrawFrame with a scene containing only a camera does not crash.
 */
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

	// Verify active camera is set
	ASSERT_NE(scene.GetActiveCamera(), nullptr);

	EXPECT_NO_THROW(m_renderer->DrawFrame(scene));

	m_renderer->WaitIdle();
}

// ---------------------------------------------------------------------------
// 3. DrawFrame with camera + mesh — frame completes
// ---------------------------------------------------------------------------

/**
 * @test DrawFrame with a scene containing a camera and a mesh completes without crash.
 */
TEST_F(SceneWiringTest, DrawFrame_SceneWithCameraAndMesh_RendersFrame)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NO_FATAL_FAILURE(CreateRenderer(kRenderWidth, kRenderHeight));

	Scene scene;

	// Camera
	auto cam = CreateDefaultCamera();
	scene.UseCamera(cam);

	// Mesh (upload to cache, then register in scene)
	auto mesh = CreateAndUploadTriangleMesh();
	ASSERT_NE(mesh, nullptr);
	scene.UseMesh(mesh);

	// Light (so lighting pass has something to compute)
	auto light = std::make_shared<Light>(LightType::POINTLIGHT, 30.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(2.0f, 5.0f, 2.0f));
	scene.UseLight(light);

	// Upload lights to resource cache
	m_resourceCache->UploadLights(scene);

	// Draw a frame
	EXPECT_NO_THROW(m_renderer->DrawFrame(scene));

	// Draw a second frame to exercise fence-guarded in-flight cycling
	EXPECT_NO_THROW(m_renderer->DrawFrame(scene));

	m_renderer->WaitIdle();
}
