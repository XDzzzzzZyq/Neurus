#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "controllers/Controllers.h"
#include "editor/events/EventBus.h"
#include "editor/operations/HistoryView.h"
#include "editor/operations/OperationManager.h"
#include "core/ResourceManager.h"
#include "render/RenderConfig.h"
#include "scene/EditorContext.h"

// Forward declarations (no render headers!)
namespace neurus {
class DeferredRenderer;
class Scene;
class UploadManager;
class UID;
struct ShaderCreateRequested;
}

namespace neurus {

/**
 * @brief Editor orchestrator — owns scene and render state, scene lifecycle,
 *        scene mutations, and UI signal wiring.
 *
 * Owns Scene and RenderConfig directly. Persistence (project load/save) lives
 * in the Application layer, which drives the scene load lifecycle via
 * BeginLoad()/FinishLoad() and NewScene().
 * Accesses DeferredRenderer and UploadManager via non-owning pointers
 * for mesh/light/IBL GPU uploads.
 */
class Editor
{
public:
	Editor(DeferredRenderer* renderer, UploadManager* uploadManager);
	~Editor();

	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;

	void Initialize();

	// --- Scene lifecycle ---
	void CreateDefaultScene(const std::string& objPath);
	void NewScene();
	void BeginLoad();
	void FinishLoad();

	// --- Accessors ---
	Scene& GetScene();
	RenderConfig& GetRenderConfig() { return m_config; }
	bool IsDirty() const { return m_dirty; }
	void MarkDirty() { m_dirty = true; }
	void ClearDirty() { m_dirty = false; }

	/**
	 * @brief Sets the asset directory used to resolve resource paths.
	 * @note The Editor resolves relative resource paths (mesh/IBL) against
	 *       this directory when creating pooled resources.
	 */
	void SetAssetDir(const std::string& dir)
	{
		m_assetDir = dir;
	}

	/**
	 * @brief Returns the app-scoped ResourceManager (UID object pool).
	 *
	 * The Application registers a project::ResourceComponent + SceneComponent
	 * against this so the pool is saved first and the Scene resolves its ID
	 * references against it on load.
	 */
	ResourceManager& GetResourceManager() { return *m_resources; }

	/**
	 * @brief Returns the shared editor state (scene + render config).
	 *
	 * The Application embeds this into both RenderContext and UIContext each
	 * frame, so the Editor is the single source of truth for scene/config and
	 * never builds a RenderContext or UIContext itself.
	 */
	EditorContext GetContext() const;

	/**
	 * @brief Returns a read-only snapshot of the undo/redo history.
	 *
	 * The Application carries this into UIContext each frame so the History
	 * panel can display the stacks without touching Editor-owned operations.
	 * It is intentionally kept out of EditorContext because the Renderer has
	 * no use for history state.
	 */
	HistoryView GetHistory() const { return ed_operations.GetHistoryView(); }

	/**
	 * @brief Returns the undo/redo manager (for project history persistence).
	 *
	 * The Application registers a project::HistoryComponent against this so the
	 * operation stacks are saved/loaded alongside the scene and render config.
	 */
	OperationManager& GetOperations() { return ed_operations; }

	template<typename T>
	void RegisterController()
	{
		auto ctrl = std::make_unique<T>();
		ctrl->Init(ed_eventBus, ed_operations);
		ed_controllers.push_back(std::move(ctrl));
	}

	void Edit();

	template<typename Event>
	void OnUIEvent(const Event& e){
		ed_eventBus.enqueue<Event>(e);
	}

	void HandleResize(uint32_t width, uint32_t height);
	void UploadSceneResources();
	void UploadLighting();

private:
	// --- Handlers called by EventQueue subscribers in Initialize() ---
	void OnMeshImport(const std::string& path);
	void OnCameraAdd();
	void OnLightAdd();
	void OnSunLightAdd();
	void OnSpotLightAdd();
	void OnIBLLoad();
	void OnCreateShader(const ShaderCreateRequested& e);
	void OnSceneObjectGpuUpload(const UID* object);

	// --- Owned state ---
	std::unique_ptr<Scene> m_scene;
	std::unique_ptr<ResourceManager> m_resources;  ///< App-scoped UID object pool
	RenderConfig          m_config;
	std::string           m_assetDir;
	bool                  m_dirty = false;

	// --- Editor infrastructure ---
	EventQueue ed_eventBus;                        ///< Editor-owned event dispatch queue.
	OperationManager ed_operations;                ///< Undo/redo history over event-replay ops.
	std::vector<std::unique_ptr<Controllers>> ed_controllers;

	// --- Non-owning references ---
	DeferredRenderer* ed_renderer = nullptr;
	UploadManager* ed_uploadManager = nullptr;
};

} // namespace neurus
