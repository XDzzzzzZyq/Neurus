/**
 * @file main.cpp
 * @brief Application entry point for the Neurus renderer.
 *
 * Initialization sequence:
 *   1. QApplication — Qt event loop (Widgets + QML)
 *   2. EventBus — singleton cross-layer communication
 *   3. VulkanContext (Phase 1) — VkInstance creation
 *   4. NeurusMainWindow + VulkanWidget — Qt window with dockable Viewport for Vulkan surface
 *   5. Show main window — apply QMainWindow layout so widget has final size
 *   6. VkSurfaceKHR — created from VulkanWidget's native HWND via VK_KHR_win32_surface
 *   7. VulkanContext (Phase 2) — logical device + queue selection
 *   8. Renderer — swapchain, pipeline, shaders (at correct window size)
 *   9. QTimer-driven render loop — ~60 FPS
 *
 * Cleanup order (CRITICAL: destroy surface BEFORE instance):
 *   renderer -> surface -> mainWindow -> vkContext
 */

// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <QApplication>
#include <DockWidget.h>
#include <QVulkanWindow>
#include <QTimer>

#include <windows.h>

#include <iostream>
#include <memory>

#include "editor/events/UIEvents.h"
#include "editor/events/EventBus.h"
#include "ui/NeurusMainWindow.h"
#include "ui/VulkanWidget.h"
#include "ui/VulkanWindow.h"
#include "render/VulkanContext.h"
#include "render/Renderer.h"

// Generated SPIR-V shader headers
#include "triangle.vert.h"
#include "triangle.frag.h"

int main(int argc, char* argv[])
{
	// --- Qt Application ---
	QApplication app(argc, argv);
	app.setApplicationName("Neurus");
	app.setApplicationVersion("0.1.0");

	// --- UIEvents (must be created first — used by all layers) ---
	auto& uiEvents = neurus::UIEvents::instance();

	// --- Two-phase Vulkan initialization ---
	// Phase 1: Create VkInstance (needed before surface)
	std::unique_ptr<neurus::NeurusMainWindow> mainWindow;
	std::unique_ptr<neurus::VulkanContext> vkContext;
	std::unique_ptr<neurus::Renderer> renderer;
	std::unique_ptr<vk::raii::SurfaceKHR> surface;
	std::unique_ptr<QVulkanInstance> qVkInstance;
	neurus::VulkanWidget* vulkanWidget = nullptr;  // Owned by mainWindow's Viewport CDockWidget
	ads::CDockWidget* viewportDock = nullptr;       // Saved for later widget swap

	try
	{
		// Step 1: Create VkInstance, immediately store in VulkanContext (no moves after surface creation)
		auto vkInstance = neurus::VulkanContext::CreateInstance();
		vkContext = std::make_unique<neurus::VulkanContext>(std::move(vkInstance));

		// Step 2: Create Qt window with VulkanWidget embedded in a dockable Viewport
		//         VulkanWidget provides the native HWND for Vulkan surface creation.
		mainWindow = std::make_unique<neurus::NeurusMainWindow>();
		vulkanWidget = new neurus::VulkanWidget();
		mainWindow->setViewportWidget(vulkanWidget);
		viewportDock = mainWindow->getViewportDock();

		// Set explicit initial size before native window creation.
		// Without this, QWidget::width()/height() return default values
		// and the swapchain would be created with wrong dimensions.
		vulkanWidget->resize(800, 600);

		// Force native window handle creation before surface creation
		vulkanWidget->winId();

		// Show window BEFORE surface/swapchain creation so QMainWindow's layout
		// is applied and the widget has its final size. This avoids swapchain
		// extent mismatch that would cause constant VK_SUBOPTIMAL_KHR.
		mainWindow->show();
		app.processEvents();  // Ensure native window is fully realized

		// Step 3: Create VkSurfaceKHR from VulkanWidget's native HWND
		HINSTANCE hinstance = GetModuleHandle(nullptr);
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, vulkanWidget->hwnd());
		surface = std::make_unique<vk::raii::SurfaceKHR>(vkContext->instance(), surfaceCreateInfo);

		// Step 4: Create logical device (needs surface for queue family selection)
		vkContext->initDevice(*surface);

		uiEvents.setGpuName(QString::fromStdString(vkContext->gpuName()));
	}
	catch (const std::exception& e)
	{
		std::cerr << "Vulkan initialization failed: " << e.what() << "\n";
		return -1;
	}

	// --- Create renderer ---
	try
	{
		renderer = std::make_unique<neurus::Renderer>(
			vkContext->device(),
			vkContext->physicalDevice(),
			vkContext->graphicsQueue(),
			vkContext->graphicsQueueFamily(),
			*surface,
			vulkanWidget->width(),
			vulkanWidget->height(),
			triangle_vert_spv,
			triangle_vert_spv_size,
			triangle_frag_spv,
			triangle_frag_spv_size
		);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Renderer initialization failed: " << e.what() << "\n";
		return -1;
	}

	// Window was shown earlier (before surface/swapchain creation) — no need to show again

	// --- QVulkanWindow-based rendering (parallel path) ---
	qVkInstance = std::make_unique<QVulkanInstance>();
	qVkInstance->setLayers({ "VK_LAYER_KHRONOS_validation" });
	if (!qVkInstance->create())
		qFatal("Failed to create QVulkanInstance");

	auto* vulkanWindow = new VulkanWindow(qVkInstance.get(),
		triangle_vert_spv, triangle_vert_spv_size,
		triangle_frag_spv, triangle_frag_spv_size);
	QWidget* viewportContainer = QWidget::createWindowContainer(vulkanWindow);

	// Replace the old VulkanWidget in the viewport dock with the QVulkanWindow container
	viewportDock->setWidget(viewportContainer, ads::CDockWidget::ForceNoScrollArea);

	// --- Connect UIEvents signals ---
	QObject::connect(&uiEvents, &neurus::UIEvents::renderRequested,
	                 [&renderer]() {
	                     if (renderer)
	                     {
	                         try
	                         {
	                             renderer->DrawFrame();
	                         }
	                         catch (...)
	                         {
	                             // Frame failed — continue
	                         }
	                     }
	                 });

	// Handle VulkanWidget resize — trigger a frame draw; swapchain recreation
	// is handled internally by DrawFrame when VK_ERROR_OUT_OF_DATE_KHR is received.
	// Re-entrancy guard: shared across resize and timer callbacks to prevent
	// nested DrawFrame() calls when processEvents() pumps the event queue.
	static bool s_frameInProgress = false;

	QObject::connect(vulkanWidget, &neurus::VulkanWidget::resized,
	                 [&renderer, &app](int /*width*/, int /*height*/) {
	                     if (s_frameInProgress || !renderer)
	                     {
	                         return;
	                     }

	                     s_frameInProgress = true;
	                     try
	                     {
	                         renderer->DrawFrame();
	                     }
	                     catch (...)
	                     {
	                         // Swapchain recreation handled inside DrawFrame
	                     }
	                     app.processEvents();
	                     s_frameInProgress = false;
	                 });

	// --- Timer-driven render loop ---
	QTimer renderTimer;
	renderTimer.setInterval(16);  // ~60 FPS
	QObject::connect(&renderTimer, &QTimer::timeout, [&renderer, &app]() {
		if (s_frameInProgress || !renderer)
		{
			return;
		}

		s_frameInProgress = true;
		try { renderer->DrawFrame(); } catch (...) {}

		// Pump pending Qt events so the window remains responsive
		// (drag, close, minimize, click) even under GPU backpressure.
		// The guard above prevents re-entrant DrawFrame calls from cascading.
		app.processEvents();

		s_frameInProgress = false;
	});
	renderTimer.start();

	// --- Run application ---
	int result = app.exec();

	// --- Clean shutdown (CRITICAL: destroy surface BEFORE instance) ---
	// C++ destruction order is reverse of declaration order for unique_ptrs,
	// but we override it explicitly to ensure VkSurfaceKHR is destroyed
	// before VkInstance.
	renderer.reset();      // 1. Destroy swapchain, pipeline, command buffers
	surface.reset();       // 2. Destroy VkSurfaceKHR
	mainWindow.reset();    // 3. Destroy main window (deletes VulkanWidget)
	vkContext.reset();     // 4. Destroy VkDevice + VkInstance

	return result;
}
