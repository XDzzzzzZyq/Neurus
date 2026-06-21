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
 * Cleanup order (CRITICAL: destroy GPU child objects BEFORE device, surface BEFORE instance):
 *   renderer -> editor -> surface -> mainWindow -> vkContext
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
#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Log.h"
#include "editor/Editor.h"
#include "editor/Input.h"
#include "editor/events/UIEvents.h"
#include "editor/events/EventBus.h"
#include "ui/NeurusMainWindow.h"
#include "ui/VulkanWidget.h"
#include "render/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "render/Texture.h"
#include "asset/MeshData.h"
#include "scene/Scene.h"
#include "project/Project.h"

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

Application::Application(int argc, char* argv[])
	: m_argc(argc)
	, m_argv(argv)
{
}

Application::~Application()
{
	// Cleanup in strict order: destroy GPU child objects BEFORE device,
	// surface BEFORE instance.
	m_renderer.reset();     // 1. DeferredRenderer -> WaitIdle(), swapchain, pipeline
	m_editor.reset();       // 2. Editor -> Context -> Project -> Scene -> Mesh -> ReleaseGPUBuffers
	m_surface.reset();      // 3. VkSurfaceKHR
	m_mainWindow.reset();   // 4. Main window (deletes VulkanWidget)
	m_vkContext.reset();    // 5. VkDevice + VkInstance (all child objects now freed)
}

int Application::Run()
{
	// --- Qt Application ---
	QApplication qtApp(m_argc, m_argv);
	qtApp.setApplicationName("Neurus");
	qtApp.setApplicationVersion("0.1.0");

	// --- UIEvents (must be created first - used by all layers) ---
	auto& uiEvents = neurus::UIEvents::instance();

	// --- Two-phase Vulkan initialization ---
	neurus::VulkanWidget* vulkanWidget = nullptr;  // Owned by mainWindow's Viewport CDockWidget

	try
	{
		// Step 1: Create VkInstance
		auto vkInstance = neurus::VulkanContext::CreateInstance();
		m_vkContext = std::make_unique<neurus::VulkanContext>(std::move(vkInstance));

		// Step 2: Create Qt window with VulkanWidget
		m_mainWindow = std::make_unique<neurus::NeurusMainWindow>();
		vulkanWidget = new neurus::VulkanWidget();
		m_mainWindow->setViewportWidget(vulkanWidget);

		vulkanWidget->resize(800, 600);
		vulkanWidget->winId();  // Force native window handle creation

		// Step 3: Create VkSurfaceKHR from VulkanWidget's native HWND
		HINSTANCE hinstance = GetModuleHandle(nullptr);
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, vulkanWidget->hwnd());
		m_surface = std::make_unique<vk::raii::SurfaceKHR>(m_vkContext->instance(), surfaceCreateInfo);

		// Step 4: Create logical device
		m_vkContext->initDevice(*m_surface);

		uiEvents.setGpuName(QString::fromStdString(m_vkContext->gpuName()));
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Vulkan initialization failed: " << e.what());
		return -1;
	}

	// --- Load or create project ---
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

	auto& scene = project->GetScene();

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

	// --- Upload each Mesh object directly to GPU ---
	for (const auto& [id, mesh] : scene.mesh_list)
	{
		mesh->UploadToGPU(m_vkContext->device(), m_vkContext->physicalDevice(),
		                  m_vkContext->graphicsQueue(), m_vkContext->graphicsQueueFamily());
	}

	// --- Create deferred renderer ---
	try
	{
		m_renderer = std::make_unique<neurus::DeferredRenderer>(
			m_vkContext->device(),
			m_vkContext->physicalDevice(),
			m_vkContext->graphicsQueue(),
			m_vkContext->graphicsQueueFamily(),
			*m_surface,
			vulkanWidget->width(),
			vulkanWidget->height()
		);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("DeferredRenderer initialization failed: " << e.what());
		return -1;
	}

	// --- Upload scene lights to GPU (via LightingPass) ---
	m_renderer->UploadLights(scene);

	// --- Create Editor and transfer project ownership ---
	m_editor = std::make_unique<neurus::Editor>(m_vkContext.get(), m_renderer.get());
	m_editor->SetProject(std::move(project));
	m_editor->Initialize(scene);

	NEURUS_LOG("[Application] Editor initialized, IBL handled by Editor");

	// Window was created hidden - show it now that the renderer is ready
	m_mainWindow->show();

	// --- Connect UIEvents signals ---
	QObject::connect(&uiEvents, &neurus::UIEvents::renderRequested,
	                 [this]() {
	                     if (m_renderer && m_editor)
	                     {
	                         Input::UpdateState();
	                         try { m_renderer->DrawFrame(m_editor->GetScene()); }
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

	// --- Timer-driven render loop ---
	QTimer renderTimer;
	renderTimer.setInterval(16);  // ~60 FPS
	QObject::connect(&renderTimer, &QTimer::timeout, [this]() {
		Input::UpdateState();
		if (m_renderer && m_editor)
		{
			try { m_renderer->DrawFrame(m_editor->GetScene()); }
			catch (const std::exception& e) { NEURUS_ERR("DrawFrame failed: " << e.what()); }
		}
		// Dispatch all queued cross-layer events (e.g. SceneStatusChanged, ObjectSelected)
		neurus::EventQueue().Process();
	});
	renderTimer.start();

	// --- Run application ---
	int result = qtApp.exec();

	return result;
}

} // namespace neurus
