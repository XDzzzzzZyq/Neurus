#pragma once

#include <memory>

class QApplication;
class QTimer;

// Forward declarations
namespace vk::raii { class SurfaceKHR; }

namespace neurus {

class VulkanContext;
class DeferredRenderer;
class Editor;
class NeurusMainWindow;

namespace project { class Project; }

/**
 * @brief Application lifecycle manager – fully RAII.
 *
 * Constructor creates the Qt application.  Run() initialises all GPU /
 * Editor subsystems, wires signals, and enters the event loop.  The
 * destructor relies on C++ reverse-order member destruction to tear
 * down GPU resources in the required order:
 *   renderer → editor → surface → mainWindow → vkContext
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
	 * @brief Creates the QApplication and render timer.
	 * @param argc Argument count.
	 * @param argv Argument vector.
	 */
	Application(int argc, char* argv[]);

	/** @brief Auto-destroy in reverse declaration order (renderer first). */
	~Application();

	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;

	/**
	 * @brief Initialises Vulkan / Editor / render loop and runs the event loop.
	 * @return Application exit code (0 on success, -1 on fatal error).
	 */
	int Run();

private:
	// --- Initialisation helpers (called in order by Run()) ---
	bool InitVulkan();
	std::unique_ptr<project::Project> LoadProject();
	bool InitRenderer(const project::Project& project);
	void InitEditor(std::unique_ptr<project::Project> project);
	void WireSignals();
	void StartRenderLoop();

	// --- Qt infrastructure (destroyed after GPU stack) ---
	std::unique_ptr<QApplication>         m_qtApp;
	std::unique_ptr<QTimer>               m_renderTimer;

	// --- GPU / UI stack (destroyed in REVERSE order: renderer first, vkContext last) ---
	std::unique_ptr<VulkanContext>        m_vkContext;       // 1st declared → destroyed 5th (LAST)
	std::unique_ptr<NeurusMainWindow>     m_mainWindow;      // 2nd declared → destroyed 4th
	std::unique_ptr<vk::raii::SurfaceKHR> m_surface;         // 3rd declared → destroyed 3rd
	std::unique_ptr<Editor>               m_editor;          // 4th declared → destroyed 2nd
	std::unique_ptr<DeferredRenderer>     m_renderer;        // 5th declared → destroyed 1st (FIRST)
};

} // namespace neurus
