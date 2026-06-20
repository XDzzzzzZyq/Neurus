#pragma once

#include <memory>
#include <QString>

// Forward declarations (no render headers!)
namespace neurus {
class VulkanContext;
class DeferredRenderer;
class Context;
class Scene;
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

	Scene& GetScene();
	neurus::project::Project& GetProject();

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

	// --- Owned ---
	std::unique_ptr<neurus::project::Project> m_project;
	std::unique_ptr<Context> m_context;

	// --- Non-owning GPU infrastructure ---
	VulkanContext* m_vkContext = nullptr;
	DeferredRenderer* m_renderer = nullptr;
};

} // namespace neurus
