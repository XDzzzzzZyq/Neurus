#include "Editor.h"

#include <QCoreApplication>

#include "editor/Context.h"
#include "editor/events/EventBus.h"
#include "editor/events/UIEvents.h"
#include "project/Project.h"
#include "scene/Scene.h"
#include "render/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "core/Log.h"

namespace {

/**
 * @brief Resolves a resource path relative to the executable directory.
 *
 * The res/ folder is copied alongside the executable by CMake at build time.
 * Resources are resolved as: {exeDir}/res/{relativePath}
 *
 * @param relativePath Path relative to res/ (e.g., "obj/sphere.obj").
 * @return Absolute path to the resource.
 */
static QString resolveResourcePath(const char* relativePath)
{
	return QCoreApplication::applicationDirPath() + "/res/" + relativePath;
}

} // anonymous namespace

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

	// --- Wire project file signal handlers ---
	auto& uiEvents = neurus::UIEvents::instance();

	// Handle New Project (Ctrl+N)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectNewRequested,
		[this]() { OnProjectNew(); });

	// Handle Open Project (Ctrl+O)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectOpenRequested,
		[this](const QString& path) { OnProjectOpen(path); });

	// Handle Save (Ctrl+S)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectSaveRequested,
		[this]() { OnProjectSave(); });

	// Handle Save As (Ctrl+Shift+S)
	QObject::connect(&uiEvents, &neurus::UIEvents::projectSaveAsRequested,
		[this](const QString& path) { OnProjectSaveAs(path); });

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

// --- Project signal handlers ---

void Editor::OnProjectNew()
{
	try
	{
		m_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::New());
		NEURUS_LOG("[Editor] Created new project.");

		// Upload scene data to GPU
		auto& projectScene = m_project->GetScene();
		for (const auto& [id, mesh] : projectScene.mesh_list)
		{
			mesh->UploadToGPU(m_vkContext->device(), m_vkContext->physicalDevice(),
			                  m_vkContext->graphicsQueue(), m_vkContext->graphicsQueueFamily());
		}
		if (m_renderer)
		{
			m_renderer->UploadLights(projectScene);
		}

		if (m_context)
		{
			m_context->editor.SetScene(&m_project->GetScene());
		}
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Failed to create new project: " << e.what());
	}
}

void Editor::OnProjectOpen(const QString& path)
{
	try
	{
		m_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::Open(path.toStdString(),
			                               resolveResourcePath("").toStdString()));
		NEURUS_LOG("[Editor] Opened project: " << path.toStdString());

		// Re-upload scene data to GPU
		auto& projectScene = m_project->GetScene();
		for (const auto& [id, mesh] : projectScene.mesh_list)
		{
			mesh->UploadToGPU(m_vkContext->device(), m_vkContext->physicalDevice(),
			                  m_vkContext->graphicsQueue(), m_vkContext->graphicsQueueFamily());
		}
		if (m_renderer)
		{
			m_renderer->UploadLights(projectScene);
		}

		if (m_context)
		{
			m_context->editor.SetScene(&m_project->GetScene());
		}
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Failed to open project: " << e.what());
	}
}

void Editor::OnProjectSave()
{
	if (m_project)
	{
		try { m_project->Save(); }
		catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
	}
}

void Editor::OnProjectSaveAs(const QString& path)
{
	if (m_project)
	{
		try { m_project->Save(path.toStdString()); }
		catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
	}
}

// --- Remaining stub handlers (implemented in later tasks) ---
void Editor::OnMeshImport(const QString& path) { NEURUS_LOG("[Editor] OnMeshImport stub: " << path.toStdString()); }
void Editor::OnCameraAdd() { NEURUS_LOG("[Editor] OnCameraAdd stub"); }
void Editor::OnLightAdd() { NEURUS_LOG("[Editor] OnLightAdd stub"); }
void Editor::OnScreenshotRequested() { NEURUS_LOG("[Editor] OnScreenshotRequested stub"); }
void Editor::OnScreenshotAllRequested() { NEURUS_LOG("[Editor] OnScreenshotAllRequested stub"); }
void Editor::OnIBLLoad() { NEURUS_LOG("[Editor] OnIBLLoad stub"); }

} // namespace neurus
