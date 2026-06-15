/**
 * @file main.cpp
 * @brief Application entry point for the Neurus renderer.
 *
 * Initialization sequence:
 *   1. QApplication — Qt event loop (Widgets + QML)
 *   2. EventBus — singleton cross-layer communication
 *   3. VulkanContext (Phase 1) — VkInstance creation
 *   4. NeurusMainWindow + VulkanWidget — Qt window with native HWND for Vulkan surface
 *   5. VkSurfaceKHR — created from VulkanWidget's native HWND via VK_KHR_win32_surface
 *   6. VulkanContext (Phase 2) — logical device + queue selection
 *   7. Renderer — swapchain, pipeline, shaders
 *   8. Show main window — only after rendering is fully initialized
 *   9. QTimer-driven render loop — ~60 FPS
 *
 * Cleanup order (CRITICAL: destroy surface BEFORE instance):
 *   renderer -> surface -> mainWindow -> vkContext
 */

// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include <QApplication>
#include <QTimer>

#include <windows.h>

#include <iostream>
#include <memory>

#include "editor/EventBus.h"
#include "ui/NeurusMainWindow.h"
#include "ui/VulkanWidget.h"
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

	// --- EventBus (must be created first — used by all layers) ---
	auto& bus = neurus::EventBus::instance();

	// --- Two-phase Vulkan initialization ---
	// Phase 1: Create VkInstance (needed before surface)
	std::unique_ptr<neurus::NeurusMainWindow> mainWindow;
	std::unique_ptr<neurus::VulkanContext> vkContext;
	std::unique_ptr<neurus::Renderer> renderer;
	std::unique_ptr<vk::raii::SurfaceKHR> surface;
	neurus::VulkanWidget* vulkanWidget = nullptr;  // Owned by mainWindow via setCentralWidget

	try
	{
		// Step 1: Create VkInstance, immediately store in VulkanContext (no moves after surface creation)
		auto vkInstance = neurus::VulkanContext::CreateInstance();
		vkContext = std::make_unique<neurus::VulkanContext>(std::move(vkInstance));

		// Step 2: Create Qt window with VulkanWidget as central widget
		//         VulkanWidget provides the native HWND for Vulkan surface creation.
		mainWindow = std::make_unique<neurus::NeurusMainWindow>();
		vulkanWidget = new neurus::VulkanWidget();
		mainWindow->setCentralWidget(vulkanWidget);

		// Set explicit initial size before native window creation.
		// Without this, QWidget::width()/height() return default values
		// and the swapchain would be created with wrong dimensions.
		vulkanWidget->resize(800, 600);

		// Force native window handle creation before surface creation
		vulkanWidget->winId();

		// Step 3: Create VkSurfaceKHR from VulkanWidget's native HWND
		HINSTANCE hinstance = GetModuleHandle(nullptr);
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, vulkanWidget->hwnd());
		surface = std::make_unique<vk::raii::SurfaceKHR>(vkContext->instance(), surfaceCreateInfo);

		// Step 4: Create logical device (needs surface for queue family selection)
		vkContext->initDevice(*surface);

		bus.setGpuName(QString::fromStdString(vkContext->gpuName()));
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

	// Window was created hidden — show it now that the renderer is ready
	mainWindow->show();

	// --- Connect EventBus signals ---
	QObject::connect(&bus, &neurus::EventBus::renderRequested,
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
	QObject::connect(vulkanWidget, &neurus::VulkanWidget::resized,
	                 [&renderer](int /*width*/, int /*height*/) {
	                     if (renderer)
	                     {
	                         try
	                         {
	                             renderer->DrawFrame();
	                         }
	                         catch (...)
	                         {
	                             // Swapchain recreation handled inside DrawFrame
	                         }
	                     }
	                 });

	// --- Timer-driven render loop ---
	QTimer renderTimer;
	renderTimer.setInterval(16);  // ~60 FPS
	QObject::connect(&renderTimer, &QTimer::timeout, [&renderer]() {
		if (renderer)
		{
			try { renderer->DrawFrame(); } catch (...) {}
		}
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
