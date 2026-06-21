#pragma once

#include <memory>
#include <vector>
#include <QString>

#include "controllers/Controllers.h"
#include "editor/Input.h"   // InputState

// Forward declarations (no render headers!)
namespace neurus {
class VulkanContext;
class DeferredRenderer;
class Context;
class Scene;
class Environment;
}

namespace neurus::project {
class Project;
}

namespace neurus {

/**
 * @brief Editor orchestrator — owns project lifecycle, scene mutations, and UI signal wiring.
 *
 * Extracted from Application.cpp to separate application logic (Editor)
 * from GPU infrastructure (Application). Owns Project + Context.
 * Accesses VulkanContext and DeferredRenderer via non-owning pointers
 * for mesh/light/IBL GPU uploads.
 */
class Editor
{
public:
	Editor(VulkanContext* vkCtx, DeferredRenderer* renderer);
	~Editor();

	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;

	void Initialize(Scene& scene);

	/**
	 * @brief Takes ownership of an existing project.
	 * Used by Application to transfer the initial project into Editor ownership.
	 * @param project Unique pointer to the project to adopt.
	 */
	void SetProject(std::unique_ptr<neurus::project::Project> project);

	Scene& GetScene();
	neurus::project::Project& GetProject();

	/**
	 * @brief Registers a controller of type T, calls Init(bus), and stores it.
	 * @tparam T Controller type (must derive from Controllers).
	 * @param bus EventQueue to pass to the controller's Init().
	 */
	template<typename T>
	void RegisterController(EventQueue& bus)
	{
		auto ctrl = std::make_unique<T>();
		ctrl->Init(bus);
		m_controllers.push_back(std::move(ctrl));
	}

	/**
	 * @brief Translates raw InputState into CameraEvents and dispatches them.
	 *
	 * Reads modifier keys and mouse deltas from InputState, enqueues the
	 * appropriate CameraEvent (rotate, push, slide, zoom), then calls
	 * EventQueue().Process() to dispatch to CameraController handlers.
	 *
	 * @param input Raw input state from Input::GetInputState().
	 */
	void Edit(const InputState& input);

private:
	// --- Signal handlers (implemented in later tasks) ---
	void OnProjectNew();
	void OnProjectOpen(const QString& path);
	void OnProjectSave();
	void OnProjectSaveAs(const QString& path);
	void OnMeshImport(const QString& path);
	void OnCameraAdd();
	void OnLightAdd();
	void OnScreenshotRequested();
	void OnScreenshotAllRequested();
	void OnIBLLoad();
	void GenerateIBL(const std::shared_ptr<Environment>& env);  ///< Shared IBL generation: loads HDR/fallback, generates cubemaps via IBLPass

	// --- Owned ---
	std::unique_ptr<neurus::project::Project> m_project;
	std::unique_ptr<Context> m_context;
	std::vector<std::unique_ptr<Controllers>> m_controllers;

	// --- Non-owning references ---
	VulkanContext* m_vkContext = nullptr;
	DeferredRenderer* m_renderer = nullptr;
	Scene* m_ownerScene = nullptr;  ///< Scene passed to Initialize()
};

} // namespace neurus
