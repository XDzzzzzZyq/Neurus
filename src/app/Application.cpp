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
 *   8. Load scene assets (sphere.obj + BAKED.png)
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
#include <QDir>
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
#include "data/MeshData.h"

// Generated SPIR-V shader headers
#include "gbuffer.vert.h"
#include "gbuffer.frag.h"
#include "pbr_lighting.comp.h"

namespace {

/**
 * @brief Resolves a resource path relative to the executable directory.
 *
 * The executable is at build/debug/Debug/Neurus.exe.
 * This walks up 3 levels to the project root, then appends \p relativePath.
 *
 * @param relativePath Path relative to the project root (e.g., "res/obj/sphere.obj").
 * @return Absolute path to the resource.
 */
static QString resolveResourcePath(const char* relativePath)
{
	QDir dir(QCoreApplication::applicationDirPath());
	dir.cdUp();  // Debug/
	dir.cdUp();  // build/debug/
	dir.cdUp();  // project root
	return dir.absoluteFilePath(relativePath);
}

/**
 * @brief Extracts the first 8 floats (pos+normal+uv) from each 14-float vertex
 *        in the MeshData interleaved array.
 *
 * @param dataArray Interleaved vertex data (14 floats per vertex).
 * @return Vector of 8-float vertices suitable for GeometryPass.
 */
static std::vector<float> extractGeometryVertices(const std::vector<float>& dataArray)
{
	constexpr size_t kSrcStride = 14;  // pos(3) + normal(3) + uv(2) + tangent(3) + bitangent(3)
	constexpr size_t kDstStride = 8;   // pos(3) + normal(3) + uv(2)
	const size_t numVertices = dataArray.size() / kSrcStride;

	std::vector<float> result(numVertices * kDstStride);
	for (size_t i = 0; i < numVertices; ++i)
	{
		std::memcpy(&result[i * kDstStride],
		            &dataArray[i * kSrcStride],
		            kDstStride * sizeof(float));
	}
	return result;
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

	// --- Load scene assets ---
	// Load sphere OBJ mesh
	neurus::MeshData sphereMeshData;
	{
		const QString objPath = resolveResourcePath("res/obj/sphere.obj");
		if (!sphereMeshData.LoadObj(objPath.toStdString()))
		{
			std::cerr << "Failed to load sphere mesh: " << objPath.toStdString() << "\n";
			return -1;
		}
	}

	// Load BAKED.png texture (for future albedo sampling; not yet wired to gbuffer.frag)
	{
		const QString texPath = resolveResourcePath("res/tex/BAKED.png");
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
			// Non-fatal - gbuffer.frag uses hardcoded albedo for MVP.
		}
	}

	// --- Extract vertex data for GeometryPass ---
	const auto& meshData = sphereMeshData.GetMeshData();
	const std::vector<float> gpuVertices = extractGeometryVertices(meshData.dataArray);
	const uint32_t vertexCount = static_cast<uint32_t>(gpuVertices.size() / 8);
	const uint32_t indexCount = static_cast<uint32_t>(meshData.indexArray.size());

	// --- Build point light data (GPU-compatible) ---
	neurus::PointLightGpu pointLight = {};
	pointLight.posX = 3.0f;
	pointLight.posY = 3.0f;
	pointLight.posZ = 3.0f;
	pointLight.colorR = 1.0f;
	pointLight.colorG = 1.0f;
	pointLight.colorB = 1.0f;
	pointLight.power = 10.0f;
	pointLight.radius = 0.05f;

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
			gpuVertices.data(),
			vertexCount,
			meshData.indexArray.data(),
			indexCount,
			pointLight,
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
	                 [this]() {
	                     if (m_renderer)
	                     {
	                         try { m_renderer->DrawFrame(); }
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
	QObject::connect(&renderTimer, &QTimer::timeout, [this]() {
		if (m_renderer)
		{
			try { m_renderer->DrawFrame(); }
			catch (const std::exception& e) { NEURUS_ERR("DrawFrame failed: " << e.what()); }
		}
		// Dispatch all queued cross-layer events (e.g. SceneStatusChanged, ObjectSelected)
		neurus::EventBus().Process();
	});
	renderTimer.start();

	// --- Run application ---
	int result = qtApp.exec();

	// --- Clean shutdown (CRITICAL: destroy surface BEFORE instance) ---
	m_renderer.reset();      // 1. Destroy DeferredRenderer (swapchain, pipeline, etc.)
	surface.reset();         // 2. Destroy VkSurfaceKHR
	mainWindow.reset();      // 3. Destroy main window (deletes VulkanWidget)
	m_vkContext.reset();     // 4. Destroy VkDevice + VkInstance

	return result;
}

} // namespace neurus
