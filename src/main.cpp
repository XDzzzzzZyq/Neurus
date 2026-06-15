#include <QGuiApplication>
#include <QTimer>

#include <iostream>
#include <memory>

#include "editor/EventBus.h"
#include "ui/MainWindow.h"
#include "render/VulkanContext.h"
#include "render/Renderer.h"

// Generated SPIR-V shader headers
#include "triangle.vert.h"
#include "triangle.frag.h"

int main(int argc, char* argv[])
{
	// --- Qt Application ---
	QGuiApplication app(argc, argv);
	app.setApplicationName("Neurus");
	app.setApplicationVersion("0.1.0");

	// --- EventBus (must be created first — used by all layers) ---
	auto& bus = neurus::EventBus::instance();

	// --- Two-phase Vulkan initialization ---
	// Phase 1: Create VkInstance (needed before surface)
	std::unique_ptr<neurus::MainWindow> mainWindow;
	std::unique_ptr<neurus::VulkanContext> vkContext;
	std::unique_ptr<neurus::Renderer> renderer;

	try
	{
		// Step 1: Create VkInstance, immediately store in VulkanContext (no moves after surface creation)
		auto vkInstance = neurus::VulkanContext::CreateInstance();
		vkContext = std::make_unique<neurus::VulkanContext>(std::move(vkInstance));

		// Step 2: Create window + surface (references ctx's instance — safe, no moves)
		mainWindow = std::make_unique<neurus::MainWindow>(vkContext->instance(), &bus);

		// Step 3: Create logical device (needs surface for queue family selection)
		vkContext->initDevice(mainWindow->surface());

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
			mainWindow->surface(),
			mainWindow->getWidth(),
			mainWindow->getHeight(),
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

	// Process initial window messages before render loop
	MSG msg;
	while (PeekMessage(&msg, mainWindow->hwnd(), 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
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

	QObject::connect(&bus, &neurus::EventBus::windowResized,
	                 [&renderer](int /*width*/, int /*height*/) {
	                 });

	// --- Timer-driven render loop (replaces QML timer) ---
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
	// C++ destruction order is reverse of declaration order.
	// VkSurfaceKHR (owned by MainWindow) must be destroyed before VkInstance (owned by VulkanContext).
	renderer.reset();      // 1. Destroy swapchain, pipeline, command buffers
	mainWindow.reset();    // 2. Destroy VkSurfaceKHR
	vkContext.reset();     // 3. Destroy VkDevice + VkInstance

	return result;
}
