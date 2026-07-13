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
class Scene;
class Environment;
class UploadManager;
class EventQueue;
struct UIContext;
struct RenderContext;
}

namespace neurus::project {
class Project;
}

namespace neurus {

/**
 * @brief Editor orchestrator — owns project lifecycle, scene mutations, and UI signal wiring.
 *
 * Extracted from Application.cpp to separate application logic (Editor)
 * from GPU infrastructure (Application). Owns Project.
 * Accesses VulkanContext and DeferredRenderer via non-owning pointers
 * for mesh/light/IBL GPU uploads.
 */
class Editor
{
public:
	Editor(VulkanContext* vkCtx, DeferredRenderer* renderer, EventQueue& eventBus);
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
	 * @brief Builds a RenderContext from the current Editor state.
	 *
	 * Sets the scene pointer but leaves width, height, and frameIndex
	 * at their default (0). The DeferredRenderer fills in the render-
	 * specific fields from the swapchain before dispatching to passes.
	 *
	 * @return RenderContext with scene populated and render fields blank.
	 */
	RenderContext GetRenderContext() const;

	/**
	 * @brief Builds a UIContext snapshot from the current Editor/Project state.
	 *
	 * Called each frame from the newFrame loop to carry RenderConfig and
	 * other editor state to the UI layer. The returned UIContext carries
	 * opaque pointers that panels cast to their concrete types.
	 *
	 * @return UIContext populated with pointers into Project-owned data.
	 */
	UIContext GetUIContext() const;

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
		ed_controllers.push_back(std::move(ctrl));
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

	/**
	 * @brief Handles viewport resize by dispatching a CameraResizeEvent.
	 *
	 * Enqueues a CameraResizeEvent with the active camera and new width/height,
	 * then processes the event queue so CameraController applies the aspect
	 * ratio change immediately.
	 *
	 * @param width  New viewport width in pixels.
	 * @param height New viewport height in pixels.
	 */
	void HandleResize(uint32_t width, uint32_t height);

	/**
	 * @brief Uploads all scene meshes and shadow-casting lights to GPU.
	 *
	 * Must be called AFTER the window is shown and the surface is ready,
	 * typically from Application::Run() after show() + ResizeViewport(),
	 * and from OnProjectOpen() / OnProjectNew().
	 */
	void UploadSceneResources();

private:
	// --- Signal handlers (implemented in later tasks) ---
	void OnProjectNew();
	void OnProjectOpen(const QString& path);
	void OnProjectSave();
	void OnProjectSaveAs(const QString& path);
	void OnMeshImport(const QString& path);
	void OnCameraAdd();
	void OnLightAdd();
	void OnSunLightAdd();
	void OnScreenshotRequested();
	void OnScreenshotAllRequested();
	void OnIBLLoad();
	void GenerateIBL(const std::shared_ptr<Environment>& env);  ///< Shared IBL generation: loads HDR/fallback, generates cubemaps via IBLPass

	// --- Owned ---
	std::unique_ptr<neurus::project::Project> ed_project;
	std::unique_ptr<UploadManager> ed_uploadManager;
	std::vector<std::unique_ptr<Controllers>> ed_controllers;

	// --- Non-owning references ---
	VulkanContext* ed_vkContext = nullptr;
	DeferredRenderer* ed_renderer = nullptr;
	EventQueue& ed_eventBus;                     ///< Application-owned EventBus (non-owning reference)
	Scene* ed_ownerScene = nullptr;  ///< Scene passed to Initialize()
};

} // namespace neurus
