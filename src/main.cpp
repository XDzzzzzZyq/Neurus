#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

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
		// Step 1: Create VkInstance
		auto vkInstance = neurus::VulkanContext::CreateInstance();

		// Step 2: Create window + surface (needs instance)
		mainWindow = std::make_unique<neurus::MainWindow>(vkInstance, &bus);

		// Step 3: Create logical device (needs surface for queue family selection)
		vkContext = std::make_unique<neurus::VulkanContext>(
			std::move(vkInstance),
			mainWindow->surface()
		);

		// --- Update GPU name in EventBus (for QML status bar) ---
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
			mainWindow->windowWidth(),
			mainWindow->windowHeight(),
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
	                     // Swapchain recreation happens inside DrawFrame()
	                     // when AcquireNextImage returns OutOfDate.
	                     // No explicit action needed here for the triangle MVP.
	                 });

	// --- Load QML UI ---
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("EventBus", &bus);

	const QUrl url("qrc:/qml/main.qml");
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
	                 &app, [&url](QObject* obj, const QUrl& objUrl) {
	                     if (!obj && url == objUrl)
	                     {
	                         QCoreApplication::exit(-1);
	                     }
	                 },
	                 Qt::QueuedConnection);

	engine.load(url);

	// --- Run application ---
	int result = app.exec();

	// --- Clean shutdown ---
	if (renderer)
	{
		renderer->WaitIdle();
	}

	return result;
}
