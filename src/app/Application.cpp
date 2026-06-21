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
 * Cleanup: C++ member destruction in reverse declaration order handles
 *   renderer → editor → surface → mainWindow → vkContext automatically.
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

// =========================================================================
// Constructor / Destructor
// =========================================================================

Application::Application(int argc, char* argv[])
{
	// --- Qt Application ---
	m_qtApp = std::make_unique<QApplication>(argc, argv);
	m_renderTimer = std::make_unique<QTimer>();
	m_qtApp->setApplicationName("Neurus");
	m_qtApp->setApplicationVersion("0.1.0");
}

Application::~Application() = default;

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

	m_mainWindow->show();

	WireSignals();
	StartRenderLoop();

	NEURUS_LOG("[Application] Entering event loop");
	int result = m_qtApp->exec();
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
		m_vkContext = std::make_unique<neurus::VulkanContext>(std::move(vkInstance));

		// Step 2: Create Qt window with VulkanWidget
		m_mainWindow = std::make_unique<neurus::NeurusMainWindow>();
		// VulkanWidget is created internally by NeurusMainWindow constructor

		// Step 3: Create VkSurfaceKHR from VulkanWidget's native HWND
		HINSTANCE hinstance = GetModuleHandle(nullptr);
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, m_mainWindow->getViewportHwnd());
		m_surface = std::make_unique<vk::raii::SurfaceKHR>(m_vkContext->instance(), surfaceCreateInfo);

		// Step 4: Create logical device
		m_vkContext->initDevice(*m_surface);

		uiEvents.setGpuName(QString::fromStdString(m_vkContext->gpuName()));
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Vulkan initialization failed: " << e.what());
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

	return project;
}

// =========================================================================
// InitRenderer – deferred renderer + light upload
// =========================================================================

bool Application::InitRenderer(const project::Project& project)
{
	try
	{
		m_renderer = std::make_unique<neurus::DeferredRenderer>(
			m_vkContext->device(),
			m_vkContext->physicalDevice(),
			m_vkContext->graphicsQueue(),
			m_vkContext->graphicsQueueFamily(),
			*m_surface,
			m_mainWindow->getViewportWidth(),
			m_mainWindow->getViewportHeight()
		);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("DeferredRenderer initialization failed: " << e.what());
		return false;
	}

	// --- Upload scene lights to GPU (via LightingPass) ---
	m_renderer->UploadLights(project.GetScene());

	return true;
}

// =========================================================================
// InitEditor – editor with project ownership transfer
// =========================================================================

void Application::InitEditor(std::unique_ptr<project::Project> project)
{
	auto& scene = project->GetScene();  // Grab reference before ownership transfer
	m_editor = std::make_unique<neurus::Editor>(m_vkContext.get(), m_renderer.get());
	m_editor->SetProject(std::move(project));
	m_editor->Initialize(scene);
	NEURUS_LOG("[Application] Editor initialized, IBL handled by Editor");
}

// =========================================================================
// WireSignals – Qt signal/slot connections
// =========================================================================

void Application::WireSignals()
{
	auto& uiEvents = neurus::UIEvents::instance();

	// --- Render request (manual frame trigger) ---
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
	QObject::connect(m_mainWindow->getVulkanWidget(), &neurus::VulkanWidget::resized,
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
}

// =========================================================================
// StartRenderLoop – timer-driven ~60 FPS render loop
// =========================================================================

void Application::StartRenderLoop()
{
	m_renderTimer->setInterval(16);  // ~60 FPS
	QObject::connect(m_renderTimer.get(), &QTimer::timeout, [this]() {
		Input::UpdateState();
		if (m_renderer && m_editor)
		{
			try { m_renderer->DrawFrame(m_editor->GetScene()); }
			catch (const std::exception& e) { NEURUS_ERR("DrawFrame failed: " << e.what()); }
		}
		// Dispatch all queued cross-layer events (e.g. SceneStatusChanged, ObjectSelected)
		neurus::EventQueue().Process();
	});
	m_renderTimer->start();
}

} // namespace neurus
