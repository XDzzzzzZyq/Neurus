/**
 * @file Application.cpp
 * @brief Application lifecycle implementation.
 *
 * Initialization sequence:
 *   1. QApplication - Qt event loop (Widgets)
 *   2. EventBus - singleton cross-layer communication
 *   3. VulkanContext (Phase 1) - VkInstance creation
 *   4. UIManager + Viewport - Qt window with dockable Viewport
 *   5. Show main window - apply layout so widget has final size
 *   6. VkSurfaceKHR - created from Viewport's native HWND
 *   7. VulkanContext (Phase 2) - logical device + queue selection
 *   8. Project::Open/Project::CreateDefault - load or create project scene + load BAKED.png texture
 *   9. DeferredRenderer - swapchain, G-Buffer, geometry pass, lighting pass, composite
 *  10. QTimer-driven render loop - ~60 FPS
 *
 * Cleanup: C++ member destruction in reverse declaration order handles
 *   renderer → editor → screenshot → surface → mainWindow → vkContext automatically.
 */

// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include "app/Application.h"

#include "VulkanContext.h"
#include "core/Log.h"
#include "editor/Editor.h"
#include "editor/Input.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/UIEvents.h"
#include "render/DeferredRenderer.h"
#include "render/RenderCache.h"
#include "render/Screenshot.h"
#include "render/Texture.h"
#include "render/shaders/ShaderLibrary.h"
#include "asset/Project.h"
#include "scene/Scene.h"
#include "ui/UIManager.h"
#include "ui/panels/Outliner.h"
#include "ui/panels/PropertyEditor.h"
#include "ui/panels/Viewport.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include <windows.h>

#include <iostream>
#include <memory>

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

// =========================================================================
// Constructor / Destructor
// =========================================================================

Application::Application(int argc, char* argv[])
{
	// --- Qt Application ---
	app_qtApp = std::make_unique<QApplication>(argc, argv);
	app_renderTimer = std::make_unique<QTimer>();
	app_renderTimer->setInterval(16);  // ~60 FPS
	app_qtApp->setApplicationName("Neurus");
	app_qtApp->setApplicationVersion("0.1.0");
}

Application::~Application()
{
	// ShaderLibrary is a Meyer's singleton (function-local static cache) that
	// outlives main(). Its cached Shader objects own vk::raii::ShaderModule
	// handles which must be destroyed BEFORE the VkDevice. Clear the cache here
	// while app_vkContext (and thus the device) is still alive; subsequent
	// reverse-order member destruction then releases the passes' shared_ptrs,
	// destroying the ShaderModules while the device still exists.
	ShaderLibrary::Clear();
}

// =========================================================================
// Run() – orchestration
// =========================================================================

int Application::Run()
{
	if (!InitVulkan())
	{
		return -1;
	}

	auto project = LoadProject();
	auto& scene = project->GetScene();

	if (!InitRenderer(*project))
	{
		return -1;
	}

	InitEditor(std::move(project));

	app_mainWindow->show();
	ResizeViewport(app_mainWindow->getViewportWidth(), app_mainWindow->getViewportHeight());

	// Upload scene GPU resources AFTER window is shown and surface is ready.
	// Doing this earlier (in Editor::Initialize) causes "Surface lost during recreation"
	// on the first frame because the swapchain isn't fully ready.
	app_editor->UploadSceneResources();

	WireSignals();

	NEURUS_LOG("[Application] Entering event loop");
	app_renderTimer->start();
	int result = app_qtApp->exec();
	return result;
}

// =========================================================================
// InitVulkan – two-phase Vulkan initialisation (Instance → Window → Surface → Device)
// =========================================================================

bool Application::InitVulkan()
{
	auto& uiEvents = neurus::UIEvents::instance();

	try
	{
		// Step 1: Create VkInstance
		auto vkInstance = neurus::VulkanContext::CreateInstance();
		app_vkContext = std::make_unique<neurus::VulkanContext>(std::move(vkInstance));

		// Step 2: Create Qt window with Viewport
		app_mainWindow = std::make_unique<neurus::UIManager>();
		// Viewport is created internally by UIManager constructor

		// Step 3: Create VkSurfaceKHR from Viewport's native HWND
		HINSTANCE hinstance = GetModuleHandle(nullptr);
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, app_mainWindow->getViewportHwnd());
		app_surface = std::make_unique<vk::raii::SurfaceKHR>(app_vkContext->instance(), surfaceCreateInfo);

		// Step 4: Create logical device
		app_vkContext->InitDevice();

		uiEvents.setGpuName(QString::fromStdString(app_vkContext->gpuName()));
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Vulkan initialization failed: " << e.what());
		return false;
	}

	try
	{
		// Step 5: Validate queue family supports presentation, get queue handle
		app_vkContext->InitQueue(*app_surface);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Queue initialization failed: " << e.what());
		return false;
	}

	return true;
}

// =========================================================================
// LoadProject – load existing or create default, upload meshes
// =========================================================================

std::unique_ptr<project::Project> Application::LoadProject()
{
	const QString projectFilePath = resolveResourcePath("shadow.neurus.json"); // Temporarily used for Rendering development and test.
	const QString objFilePath = resolveResourcePath("obj/sphere.obj");
	auto project = std::make_unique<neurus::project::Project>(neurus::project::Project::New());

	try
	{
		*project = neurus::project::Project::Open(projectFilePath.toStdString(),
		                                          resolveResourcePath("").toStdString());
		NEURUS_LOG("[Application] Loaded project: " << projectFilePath.toStdString());
	}
	catch (const std::exception& e)
	{
		NEURUS_LOG("[Application] Project file not found, creating default: " << e.what());
		project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::CreateDefault(objFilePath.toStdString()));
		// Store relative paths in the project file for portability
		for (auto& [id, mesh] : project->GetScene().mesh_list)
		{
			mesh->o_meshPath = "obj/sphere.obj";
		}
		// Save for future runs
		try { project->Save(projectFilePath.toStdString()); }
		catch (const std::exception& se) { NEURUS_ERR("Could not save default project: " << se.what()); }
	}

	// GPU resources created lazily by RenderCache on first use
	return project;
}

// =========================================================================
// InitRenderer – deferred renderer + light upload
// =========================================================================

bool Application::InitRenderer(const project::Project& project)
{
	try
	{
		app_renderer = std::make_unique<neurus::DeferredRenderer>(
			app_vkContext->device(),
			app_vkContext->physicalDevice(),
			app_vkContext->graphicsQueue(),
			app_vkContext->graphicsQueueFamily(),
			*app_surface,
			app_mainWindow->getViewportWidth(),
			app_mainWindow->getViewportHeight()
		);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("DeferredRenderer initialization failed: " << e.what());
		return false;
	}

	// --- Create screenshot helper (needs Vulkan handles; RenderCache passed per-call) ---
	app_screenshot = std::make_unique<neurus::Screenshot>(
		app_vkContext->device(),
		app_vkContext->physicalDevice(),
		app_vkContext->graphicsQueue(),
		app_vkContext->graphicsQueueFamily());

	return true;
}

void Application::ResizeViewport(int width, int height)
{
	app_renderer->HandleResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	app_editor->HandleResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

// =========================================================================
// InitEditor – editor with project ownership transfer
// =========================================================================

void Application::InitEditor(std::unique_ptr<project::Project> project)
{
	auto& scene = project->GetScene();  // Grab reference before ownership transfer
	app_editor = std::make_unique<neurus::Editor>(app_vkContext.get(), app_renderer.get(), app_eventBus);
	app_editor->SetProject(std::move(project));
	app_editor->Initialize(scene);
	NEURUS_LOG("[Application] Editor initialized, IBL handled by Editor");
}

// =========================================================================
// WireSignals – Qt signal/slot connections
// =========================================================================

void Application::WireSignals()
{
	auto& uiEvents = neurus::UIEvents::instance();

	// --- Render request (manual frame trigger) ---
	// 3-line newFrame pattern: update input → translate to events → draw
	QObject::connect(&uiEvents, &neurus::UIEvents::newFrame,
	                 [this]() {
	                     if (app_renderer && app_editor)
	                     {
	                         Input::UpdateState();
	                         app_editor->Edit(Input::GetInputState());
	                         app_eventBus.Process();  // Dispatch all enqueued events before drawing
	                         app_renderer->DrawFrame(app_editor->GetScene());
	                     }
	                 });

	// --- Outliner object selection → EventBus ---
	// UI layer emits Qt signal; Application translates to typed EventQueue event
	{
		auto* outliner = app_mainWindow->getOutliner();
		if (outliner)
		{
			QObject::connect(outliner, &neurus::Outliner::objectSelected,
			                 [this](int objectId) {
			                     app_eventBus.enqueue(neurus::ObjectSelected{objectId});
			                 });
		}
	}

	// Handle Viewport resize - proactively recreate swapchain so the
	// next DrawFrame uses the correct dimensions. The existing OutOfDateKHR
	// fallback in DrawFrame/AcquireNextImage remains as a safety net.
	QObject::connect(app_mainWindow->getViewport(), &neurus::Viewport::resized,
	                 [this](int width, int height) {
	                     ResizeViewport(width, height);
	                 });

	// Handle screenshot requests (F12 / menu action) via UIEvents signal
	QObject::connect(&uiEvents, &neurus::UIEvents::screenshotRequested,
	                 [this]() {
	                     if (app_screenshot && app_renderer)
	                     {
	                         app_screenshot->TakeScreenshot(
	                             app_renderer->GetLastSwapchainImage(),
	                             app_renderer->GetSwapchainFormat(),
	                             app_renderer->GetExtent());
	                     }
	                 });

	// Handle attachment dump requests (Ctrl+F12) via UIEvents signal
	QObject::connect(&uiEvents, &neurus::UIEvents::screenshotAllRequested,
	                 [this]() {
	                     if (app_screenshot && app_renderer)
	                     {
	                         app_screenshot->TakeScreenshotAllAttachments(
	                             app_renderer->GetRenderCache(),
	                             app_renderer->GetExtent(), 1024);
	                     }
	                 });

	// StartRenderLoop – timer-driven ~60 FPS render loop
	QObject::connect(app_renderTimer.get(), &QTimer::timeout, [&uiEvents]() {
		emit uiEvents.newFrame();
	});
}
} // namespace neurus
