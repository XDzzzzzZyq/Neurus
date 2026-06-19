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
 *   renderer -> context -> project -> surface -> mainWindow -> vkContext
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
#include "editor/Context.h"
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

// Generated SPIR-V shader headers
#include "gbuffer.vert.h"
#include "gbuffer.frag.h"
#include "pbr_lighting.comp.h"
#include "ssao.comp.h"
#include "irradiance_conv.comp.h"
#include "importance_samp.comp.h"

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
	const QString projectFilePath = resolveResourcePath("shadow.neurus.json"); // Temporarily used for Rendering development and test.
	const QString objFilePath = resolveResourcePath("obj/sphere.obj");
	m_project = std::make_unique<neurus::project::Project>(neurus::project::Project::New());
	try
	{
		*m_project = neurus::project::Project::Open(projectFilePath.toStdString(),
		                                          resolveResourcePath("").toStdString());
		NEURUS_LOG("[Application] Loaded project: " << projectFilePath.toStdString());
	}
	catch (const std::exception& e)
	{
		NEURUS_LOG("[Application] Project file not found, creating default: " << e.what());
		m_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::CreateDefault(objFilePath.toStdString()));
		// Store relative paths in the project file for portability
		for (auto& [id, mesh] : m_project->GetScene().mesh_list)
		{
			mesh->o_meshPath = "obj/sphere.obj";
		}
		// Save for future runs
		try { m_project->Save(projectFilePath.toStdString()); }
		catch (const std::exception& se) { NEURUS_ERR("Could not save default project: " << se.what()); }
	}

	auto& scene = m_project->GetScene();

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
			*surface,
			vulkanWidget->width(),
			vulkanWidget->height(),
			gbuffer_vert_spv, gbuffer_vert_spv_size,
			gbuffer_frag_spv, gbuffer_frag_spv_size,
			pbr_lighting_comp_spv, pbr_lighting_comp_spv_size,
			ssao_comp_spv, ssao_comp_spv_size,
			irradiance_conv_comp_spv, irradiance_conv_comp_spv_size,
			importance_samp_comp_spv, importance_samp_comp_spv_size
		);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("DeferredRenderer initialization failed: " << e.what());
		return -1;
	}

	// --- Upload scene lights to GPU (via LightingPass) ---
	m_renderer->UploadLights(scene);

	// Window was created hidden - show it now that the renderer is ready
	mainWindow->show();

	// --- Connect UIEvents signals ---
	QObject::connect(&uiEvents, &neurus::UIEvents::renderRequested,
	                 [this]() {
	                     if (m_renderer && m_project)
	                     {
	                         Input::UpdateState();
	                         try { m_renderer->DrawFrame(m_project->GetScene()); }
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

	// --- Project file signal wiring ---

	// Handle New Project (Ctrl+N)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectNewRequested,
		[this]() {
			try {
				m_project = std::make_unique<neurus::project::Project>(
					neurus::project::Project::New());
				NEURUS_LOG("[Application] Created new project.");

				// Upload scene data to GPU
				auto& projectScene = m_project->GetScene();
				for (const auto& [id, mesh] : projectScene.mesh_list)
				{
					mesh->UploadToGPU(m_vkContext->device(), m_vkContext->physicalDevice(),
					                  m_vkContext->graphicsQueue(), m_vkContext->graphicsQueueFamily());
				}
				if (m_renderer)
				{
					m_renderer->UploadLights(projectScene);
				}

				if (m_context)
				{
					m_context->editor.SetScene(&m_project->GetScene());
				}
			}
			catch (const std::exception& e) {
				NEURUS_ERR("Failed to create new project: " << e.what());
			}
		});

	// Handle Open Project (Ctrl+O)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectOpenRequested,
		[this](const QString& path) {
			try {
				m_project = std::make_unique<neurus::project::Project>(
					neurus::project::Project::Open(path.toStdString(),
					                               resolveResourcePath("").toStdString()));
				NEURUS_LOG("[Application] Opened project: " << path.toStdString());

				// Re-upload scene data to GPU
				auto& projectScene = m_project->GetScene();
				for (const auto& [id, mesh] : projectScene.mesh_list)
				{
					mesh->UploadToGPU(m_vkContext->device(), m_vkContext->physicalDevice(),
					                  m_vkContext->graphicsQueue(), m_vkContext->graphicsQueueFamily());
				}
				if (m_renderer)
				{
					m_renderer->UploadLights(projectScene);
				}

				if (m_context)
				{
					m_context->editor.SetScene(&m_project->GetScene());
				}
			}
			catch (const std::exception& e) {
				NEURUS_ERR("Failed to open project: " << e.what());
			}
		});

	// Handle Save (Ctrl+S)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectSaveRequested,
		[this]() {
			if (m_project)
			{
				try { m_project->Save(); }
				catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
			}
		});

	// Handle Save As (Ctrl+Shift+S)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectSaveAsRequested,
		[this](const QString& path) {
			if (m_project)
			{
				try { m_project->Save(path.toStdString()); }
				catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
			}
		});

	// --- Mesh import signal wiring ---

	// Handle mesh import (Edit → Add → Mesh...)
	QObject::connect(&uiEvents, &neurus::UIEvents::meshImportRequested,
		[this](const QString& path) {
			try {
				auto mesh = std::make_shared<neurus::Mesh>(path.toStdString());
				mesh->UploadToGPU(m_vkContext->device(), m_vkContext->physicalDevice(),
				                  m_vkContext->graphicsQueue(), m_vkContext->graphicsQueueFamily());
				m_project->GetScene().UseMesh(mesh);
				m_project->MarkDirty();
				NEURUS_LOG("[Application] Imported mesh: " << path.toStdString());
			}
			catch (const std::exception& e) {
				NEURUS_ERR("Failed to import mesh: " << e.what());
			}
		});

	// Handle camera add (Edit → Add → Camera)
	QObject::connect(&uiEvents, &neurus::UIEvents::cameraAddRequested,
		[this]() {
			try {
				auto camera = std::make_shared<neurus::Camera>();
				camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
				camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
				m_project->GetScene().UseCamera(camera);
				m_project->MarkDirty();
				NEURUS_LOG("[Application] Added camera at (0, 2, 5)");
			}
			catch (const std::exception& e) {
				NEURUS_ERR("Failed to add camera: " << e.what());
			}
		});

	// Handle light add (Edit → Add → Light)
	QObject::connect(&uiEvents, &neurus::UIEvents::lightAddRequested,
		[this]() {
			try {
				auto light = std::make_shared<neurus::Light>(
					neurus::POINTLIGHT, 10.0f, glm::vec3(1.0f));
				light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
				light->SetRadius(0.05f);
				m_project->GetScene().UseLight(light);
				if (m_renderer)
				{
					m_renderer->UploadLights(m_project->GetScene());
				}
				m_project->MarkDirty();
				NEURUS_LOG("[Application] Added point light at (3, 3, 3)");
			}
			catch (const std::exception& e) {
				NEURUS_ERR("Failed to add light: " << e.what());
			}
		});

	// --- Context (cross-layer state aggregation + event communication) ---
	// Context owns SceneContext (scene pointer), EditorContext (selections, signals, dirty
	// tracking), and RenderContext (render configs stub). It subscribes to EventBus events
	// to keep sub-contexts in sync with scene modifications.
	m_context = std::make_unique<neurus::Context>(neurus::EventBus());
	m_context->editor.SetScene(&scene);

	// --- Timer-driven render loop ---
	QTimer renderTimer;
	renderTimer.setInterval(16);  // ~60 FPS
	QObject::connect(&renderTimer, &QTimer::timeout, [this]() {
		Input::UpdateState();
		if (m_renderer && m_project)
		{
			try { m_renderer->DrawFrame(m_project->GetScene()); }
			catch (const std::exception& e) { NEURUS_ERR("DrawFrame failed: " << e.what()); }
		}
		// Dispatch all queued cross-layer events (e.g. SceneStatusChanged, ObjectSelected)
		neurus::EventBus().Process();
	});
	renderTimer.start();

	// --- Run application ---
	int result = qtApp.exec();

	// --- Clean shutdown (CRITICAL: destroy GPU child objects BEFORE device, surface BEFORE instance) ---
	m_renderer.reset();         // 1. Destroy DeferredRenderer -> WaitIdle(), swapchain, pipeline
	m_context.reset();          // 2. Destroy Context (releases scene references)
	m_project.reset();          // 3. Destroy Project -> Scene -> Mesh -> ReleaseGPUBuffers -> free VBO/IBO
	surface.reset();            // 4. Destroy VkSurfaceKHR
	mainWindow.reset();         // 5. Destroy main window (deletes VulkanWidget)
	m_vkContext.reset();        // 6. Destroy VkDevice + VkInstance (all child objects now freed)

	return result;
}

} // namespace neurus
