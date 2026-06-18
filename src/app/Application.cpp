/**
 * @file Application.cpp
 * @brief Application lifecycle implementation.
 *
 * Initialization sequence:
 *   1. QApplication - Qt event loop (Widgets)
 *   2. EventBus - singleton cross-layer communication
 *   3. VulkanContext (Phase 1) - VkInstance creation
 *   4. NeurusMainWindow + VulkanWidget - Qt window with dockable Viewport
 *   5. Show main window - apply layout so widget has final size
 *   6. VkSurfaceKHR - created from VulkanWidget's native HWND
 *   7. VulkanContext (Phase 2) - logical device + queue selection
 *   8. Project::Open/Project::CreateDefault - load or create project scene + load BAKED.png texture
 *   9. DeferredRenderer - swapchain, G-Buffer, geometry pass, lighting pass, composite
 *  10. QTimer-driven render loop - ~60 FPS
 *
 * Cleanup order (CRITICAL: destroy surface BEFORE instance):
 *   renderer -> surface -> mainWindow -> vkContext
 */

// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include "app/Application.h"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <DockWidget.h>
#include <QTimer>

#include <windows.h>

#include <iostream>
#include <memory>
#include <cstring>

#include "core/Log.h"
#include "editor/EditorContext.h"
#include "editor/events/UIEvents.h"
#include "editor/events/EventBus.h"
#include "ui/NeurusMainWindow.h"
#include "ui/VulkanWidget.h"
#include "render/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "render/Texture.h"
#include "data/GPUResourceCache.h"
#include "data/MeshData.h"
#include "scene/Scene.h"
#include "project/Project.h"

// Generated SPIR-V shader headers
#include "gbuffer.vert.h"
#include "gbuffer.frag.h"
#include "pbr_lighting.comp.h"

namespace {

/**
 * @brief Resolves a resource path relative to the executable directory.
 *
 * The res/ folder is copied alongside the executable by CMake at build time.
 * Resources are resolved as: {exeDir}/res/{relativePath}
 *
 * @param relativePath Path relative to res/ (e.g., "obj/sphere.obj").
 * @return Absolute path to the resource.
 */
static QString resolveResourcePath(const char* relativePath)
{
	return QCoreApplication::applicationDirPath() + "/res/" + relativePath;
}

} // anonymous namespace

namespace neurus {

Application::Application() = default;

Application::~Application()
{
	// m_renderer and m_vkContext are reset explicitly in Run()
	// before the Application object is destroyed, ensuring correct
	// cleanup order. This destructor is a safety net but should
	// not normally be reached with live GPU resources.
}

int Application::Run(int argc, char* argv[])
{
	// --- Qt Application ---
	QApplication qtApp(argc, argv);
	qtApp.setApplicationName("Neurus");
	qtApp.setApplicationVersion("0.1.0");

	// --- UIEvents (must be created first - used by all layers) ---
	auto& uiEvents = neurus::UIEvents::instance();

	// --- Two-phase Vulkan initialization ---
	std::unique_ptr<neurus::NeurusMainWindow> mainWindow;
	std::unique_ptr<vk::raii::SurfaceKHR> surface;
	neurus::VulkanWidget* vulkanWidget = nullptr;  // Owned by mainWindow's Viewport CDockWidget

	try
	{
		// Step 1: Create VkInstance
		auto vkInstance = neurus::VulkanContext::CreateInstance();
		m_vkContext = std::make_unique<neurus::VulkanContext>(std::move(vkInstance));

		// Step 2: Create Qt window with VulkanWidget
		mainWindow = std::make_unique<neurus::NeurusMainWindow>();
		vulkanWidget = new neurus::VulkanWidget();
		mainWindow->setViewportWidget(vulkanWidget);

		vulkanWidget->resize(800, 600);
		vulkanWidget->winId();  // Force native window handle creation

		// Step 3: Create VkSurfaceKHR from VulkanWidget's native HWND
		HINSTANCE hinstance = GetModuleHandle(nullptr);
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, vulkanWidget->hwnd());
		surface = std::make_unique<vk::raii::SurfaceKHR>(m_vkContext->instance(), surfaceCreateInfo);

		// Step 4: Create logical device
		m_vkContext->initDevice(*surface);

		uiEvents.setGpuName(QString::fromStdString(m_vkContext->gpuName()));
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Vulkan initialization failed: " << e.what());
		return -1;
	}

	// --- Load or create project ---
	const QString projectFilePath = resolveResourcePath("default.neurus.json");
	const QString objFilePath = resolveResourcePath("obj/sphere.obj");
	auto project = neurus::project::Project::New();
	try
	{
		project = neurus::project::Project::Open(projectFilePath.toStdString());
		NEURUS_LOG("[Application] Loaded project: " << projectFilePath.toStdString());

		// Reload mesh geometry from OBJ paths (not stored in JSON)
		auto& projectScene = project.GetScene();
		for (auto& [id, mesh] : projectScene.mesh_list)
		{
			if (!mesh->o_mesh && !mesh->o_meshPath.empty())
			{
				// Resolve relative path (stored in JSON) to absolute resource path
				const QString fullPath = resolveResourcePath(mesh->o_meshPath.c_str());
				auto meshData = std::make_shared<MeshData>();
				if (meshData->LoadObj(fullPath.toStdString()))
				{
					mesh->o_mesh = meshData;
				}
			}
		}
	}
	catch (const std::exception& e)
	{
		NEURUS_LOG("[Application] Project file not found, creating default: " << e.what());
		project = neurus::project::Project::CreateDefault(objFilePath.toStdString());
		// Store relative paths in the project file for portability
		for (auto& [id, mesh] : project.GetScene().mesh_list)
		{
			mesh->o_meshPath = "obj/sphere.obj";
		}
		// Save for future runs
		try { project.Save(projectFilePath.toStdString()); }
		catch (const std::exception& se) { NEURUS_ERR("Could not save default project: " << se.what()); }
	}

	auto& scene = project.GetScene();

	// Load BAKED.png texture (for future albedo sampling; not yet wired to gbuffer.frag)
	{
		const QString texPath = resolveResourcePath("tex/BAKED.png");
		auto bakedTex = neurus::Texture::FromFile(
			m_vkContext->device(),
			m_vkContext->physicalDevice(),
			m_vkContext->graphicsQueue(),
			m_vkContext->graphicsQueueFamily(),
			texPath.toStdString().c_str(),
			vk::Format::eR8G8B8A8Srgb);

		if (!bakedTex.IsValid())
		{
			std::cerr << "Warning: Failed to load texture: " << texPath.toStdString() << "\n";
		}
	}

	// --- Initialize GPU resource cache and upload scene data ---
	m_resourceCache = std::make_unique<neurus::GPUResourceCache>(
		m_vkContext->device(),
		m_vkContext->physicalDevice(),
		m_vkContext->graphicsQueue(),
		m_vkContext->graphicsQueueFamily());

	// Upload each Mesh object directly (GPUResourceCache reads MeshData from mesh->o_mesh)
	for (const auto& [id, mesh] : scene.mesh_list)
	{
		m_resourceCache->UploadMesh(*mesh);
	}

	// Upload lights (no guard — zero lights are handled gracefully by GPUResourceCache
	// and LightingPass)
	m_resourceCache->UploadLights(scene);

	// --- Create deferred renderer (uses cache for mesh/light buffers) ---
	try
	{
		m_renderer = std::make_unique<neurus::DeferredRenderer>(
			m_vkContext->device(),
			m_vkContext->physicalDevice(),
			m_vkContext->graphicsQueue(),
			m_vkContext->graphicsQueueFamily(),
			*surface,
			vulkanWidget->width(),
			vulkanWidget->height(),
			*m_resourceCache,
			gbuffer_vert_spv, gbuffer_vert_spv_size,
			gbuffer_frag_spv, gbuffer_frag_spv_size,
			pbr_lighting_comp_spv, pbr_lighting_comp_spv_size
		);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("DeferredRenderer initialization failed: " << e.what());
		return -1;
	}

	// Window was created hidden - show it now that the renderer is ready
	mainWindow->show();

	// --- Connect UIEvents signals ---
	QObject::connect(&uiEvents, &neurus::UIEvents::renderRequested,
	                 [this, &scene]() {
	                     if (m_renderer)
	                     {
	                         try { m_renderer->DrawFrame(scene); }
	                         catch (const std::exception& e) { NEURUS_ERR("DrawFrame failed: " << e.what()); }
	                     }
	                 });

	// Handle VulkanWidget resize - proactively recreate swapchain so the
	// next DrawFrame uses the correct dimensions. The existing OutOfDateKHR
	// fallback in DrawFrame/AcquireNextImage remains as a safety net.
	QObject::connect(vulkanWidget, &neurus::VulkanWidget::resized,
	                 [this](int width, int height) {
	                     if (m_renderer)
	                     {
	                         m_renderer->HandleResize(
	                             static_cast<uint32_t>(width),
	                             static_cast<uint32_t>(height));
	                     }
	                 });

	// TODO: decouple screenshot from DeferredRenderer, Isolate by Editor, Use EventBus as bridge.
	// Handle screenshot requests (F12 / menu action) via UIEvents signal
	QObject::connect(&uiEvents, &neurus::UIEvents::screenshotRequested,
	                 [this]() {
	                     if (m_renderer)
	                     {
	                         m_renderer->TakeScreenshot();
	                     }
	                 });

	// Handle attachment dump requests (Ctrl+F12) via UIEvents signal
	QObject::connect(&uiEvents, &neurus::UIEvents::screenshotAllRequested,
	                 [this]() {
	                     if (m_renderer)
	                     {
	                         m_renderer->TakeScreenshotAllAttachments();
	                     }
	                 });

	// --- Editor Context (cross-layer event communication) ---
	// EditorContext owns editor state and routes scene/selection events via EventBus.
	// EventBus().Process() must be called each frame to dispatch queued events.
	m_editorContext = std::make_unique<neurus::EditorContext>();

	// --- Timer-driven render loop ---
	QTimer renderTimer;
	renderTimer.setInterval(16);  // ~60 FPS
	QObject::connect(&renderTimer, &QTimer::timeout, [this, &scene]() {
		if (m_renderer)
		{
			try { m_renderer->DrawFrame(scene); }
			catch (const std::exception& e) { NEURUS_ERR("DrawFrame failed: " << e.what()); }
		}
		// Dispatch all queued cross-layer events (e.g. SceneStatusChanged, ObjectSelected)
		neurus::EventBus().Process();
	});
	renderTimer.start();

	// --- Run application ---
	int result = qtApp.exec();

	// --- Clean shutdown (CRITICAL: destroy surface BEFORE instance) ---
	m_renderer.reset();         // 1. Destroy DeferredRenderer (swapchain, pipeline, etc.)
	m_resourceCache.reset();    // 2. Destroy GPUResourceCache (vertex/index/light buffers)
	surface.reset();            // 3. Destroy VkSurfaceKHR
	mainWindow.reset();         // 4. Destroy main window (deletes VulkanWidget)
	m_vkContext.reset();        // 5. Destroy VkDevice + VkInstance

	return result;
}

} // namespace neurus
