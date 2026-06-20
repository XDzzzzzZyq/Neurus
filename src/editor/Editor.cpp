#include "Editor.h"

#include "editor/Context.h"
#include "editor/events/EventBus.h"
#include "editor/events/UIEvents.h"
#include "project/Project.h"
#include "scene/Scene.h"
#include "render/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "core/Log.h"

namespace neurus {

Editor::Editor(VulkanContext* vkCtx, DeferredRenderer* renderer)
	: m_vkContext(vkCtx)
	, m_renderer(renderer)
{
}

Editor::~Editor()
{
	// Destroy in order: Context (references scene) → Project (owns scene)
	m_context.reset();
	m_project.reset();
}

void Editor::Initialize(Scene& scene)
{
	// Create Context with EventQueue singleton
	m_context = std::make_unique<Context>(EventQueue());
	m_context->editor.SetScene(&scene);

	// Signal wiring will be added in later tasks
	NEURUS_LOG("[Editor] Initialized");
}

Scene& Editor::GetScene()
{
	return m_project->GetScene();
}

neurus::project::Project& Editor::GetProject()
{
	return *m_project;
}

// --- Stub signal handlers (implemented in later tasks) ---
void Editor::OnProjectNew() { NEURUS_LOG("[Editor] OnProjectNew stub"); }
void Editor::OnProjectOpen(const QString& path) { NEURUS_LOG("[Editor] OnProjectOpen stub: " << path.toStdString()); }
void Editor::OnProjectSave() { NEURUS_LOG("[Editor] OnProjectSave stub"); }
void Editor::OnProjectSaveAs(const QString& path) { NEURUS_LOG("[Editor] OnProjectSaveAs stub"); }
void Editor::OnMeshImport(const QString& path) { NEURUS_LOG("[Editor] OnMeshImport stub: " << path.toStdString()); }
void Editor::OnCameraAdd() { NEURUS_LOG("[Editor] OnCameraAdd stub"); }
void Editor::OnLightAdd() { NEURUS_LOG("[Editor] OnLightAdd stub"); }
void Editor::OnScreenshotRequested() { NEURUS_LOG("[Editor] OnScreenshotRequested stub"); }
void Editor::OnScreenshotAllRequested() { NEURUS_LOG("[Editor] OnScreenshotAllRequested stub"); }
void Editor::OnIBLLoad() { NEURUS_LOG("[Editor] OnIBLLoad stub"); }

} // namespace neurus
