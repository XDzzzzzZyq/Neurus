#pragma once

#include <memory>

namespace neurus::project {
class Project;
}

namespace neurus {

// Forward declarations
class VulkanContext;
class DeferredRenderer;
class Context;
class GPUResourceCache;

/**
 * @brief Application lifecycle manager.
 *
 * Owns the core subsystems (Vulkan, Renderer, Editor) and the main loop.
 * QApplication and QWidgets are created inside Run() and remain local to
 * the method, following Qt conventions.
 *
 * Usage:
 * @code
 *   neurus::Application app;
 *   return app.Run(argc, argv);
 * @endcode
 */
class Application
{
public:
	Application();
	~Application();

	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;

	/**
	 * @brief Runs the application: Qt event loop, Vulkan init, render loop, shutdown.
	 * @param argc Argument count (passed to QApplication).
	 * @param argv Argument vector (passed to QApplication).
	 * @return Application exit code.
	 */
	int Run(int argc, char* argv[]);

private:
	std::unique_ptr<VulkanContext> m_vkContext;
	std::unique_ptr<DeferredRenderer> m_renderer;
	std::unique_ptr<GPUResourceCache> m_resourceCache;
	std::unique_ptr<Context> m_context;
	std::unique_ptr<neurus::project::Project> m_project;
};

} // namespace neurus
