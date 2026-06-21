#pragma once

#include <memory>

// Forward declarations
namespace vk::raii { class SurfaceKHR; }

namespace neurus {

class VulkanContext;
class DeferredRenderer;
class Editor;
class NeurusMainWindow;

/**
 * @brief Application lifecycle manager – fully RAII.
 *
 * Constructor stores command-line arguments.  Run() creates the Qt event loop,
 * initialises all GPU / Editor subsystems, wires signals, and enters the
 * event loop.  The destructor tears everything down in strict dependency
 * order (renderer → editor → surface → mainWindow → vkContext).
 *
 * Usage:
 * @code
 *   neurus::Application app(argc, argv);
 *   return app.Run();
 * @endcode
 */
class Application
{
public:
	/**
	 * @brief Stores command-line arguments for later use by Run().
	 * @param argc Argument count.
	 * @param argv Argument vector.
	 */
	Application(int argc, char* argv[]);

	~Application();

	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;

	/**
	 * @brief Creates the Qt / Vulkan / Editor stack and enters the event loop.
	 *
	 * QApplication is stack-local inside this method (Qt convention).
	 * All GPU initialisation, project loading, signal wiring, and the
	 * render timer are set up here.
	 *
	 * @return Application exit code (0 on success, -1 on fatal error).
	 */
	int Run();

private:
	int m_argc;
	char** m_argv;

	std::unique_ptr<VulkanContext>      m_vkContext;
	std::unique_ptr<DeferredRenderer>   m_renderer;
	std::unique_ptr<Editor>             m_editor;
	std::unique_ptr<NeurusMainWindow>   m_mainWindow;
	std::unique_ptr<vk::raii::SurfaceKHR> m_surface;
};

} // namespace neurus
