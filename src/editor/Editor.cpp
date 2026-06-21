#include "Editor.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "editor/Context.h"
#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/UIEvents.h"
#include "project/Project.h"
#include "scene/Environment.h"
#include "scene/Scene.h"
#include "render/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "render/Image.h"
#include "render/passes/IBLPass.h"
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

void Editor::SetProject(std::unique_ptr<neurus::project::Project> project)
{
	m_project = std::move(project);
}

Editor::~Editor()
{
	// Destroy in order: Context (references scene) → Project (owns scene)
	m_context.reset();
	m_project.reset();
}

void Editor::Initialize(Scene& scene)
{
	// Store the scene reference for OnIBLLoad and other operations
	m_ownerScene = &scene;

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

	// --- Mesh/camera/light signal handlers ---

	// Handle mesh import (Edit -> Add -> Mesh...)
	QObject::connect(&uiEvents, &neurus::UIEvents::meshImportRequested,
		[this](const QString& path) { OnMeshImport(path); });

	// Handle camera add (Edit -> Add -> Camera)
	QObject::connect(&uiEvents, &neurus::UIEvents::cameraAddRequested,
		[this]() { OnCameraAdd(); });

	// Handle light add (Edit -> Add -> Light)
	QObject::connect(&uiEvents, &neurus::UIEvents::lightAddRequested,
		[this]() { OnLightAdd(); });

	// Load IBL environment now that the scene is available
	OnIBLLoad();

	// --- Subscribe to EnvironmentChanged to regenerate IBL cubemaps on demand ---
	EventQueue().subscribe<EnvironmentChanged>([this](const EnvironmentChanged& e) {
		auto it = GetScene().env_list.find(e.envId);
		if (it != GetScene().env_list.end())
		{
			GenerateIBL(it->second);
		}
		else
		{
			NEURUS_ERR("[Editor] EnvironmentChanged: env ID " << e.envId << " not found");
		}
	});

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

// --- Mesh, Camera, Light signal handlers ---

void Editor::OnMeshImport(const QString& path)
{
	try {
		auto mesh = std::make_shared<neurus::Mesh>(path.toStdString());
		mesh->UploadToGPU(m_vkContext->device(), m_vkContext->physicalDevice(),
		                  m_vkContext->graphicsQueue(), m_vkContext->graphicsQueueFamily());
		m_project->GetScene().UseMesh(mesh);
		m_project->MarkDirty();
		NEURUS_LOG("[Editor] Imported mesh: " << path.toStdString());
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to import mesh: " << e.what());
	}
}

void Editor::OnCameraAdd()
{
	try {
		auto camera = std::make_shared<neurus::Camera>();
		camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
		camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
		m_project->GetScene().UseCamera(camera);
		m_project->MarkDirty();
		NEURUS_LOG("[Editor] Added camera at (0, 2, 5)");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add camera: " << e.what());
	}
}

void Editor::OnLightAdd()
{
	try {
		auto light = std::make_shared<neurus::Light>(
			neurus::POINTLIGHT, 10.0f, glm::vec3(1.0f));
		light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
		light->SetRadius(0.05f);
		m_project->GetScene().UseLight(light);
		if (m_renderer)
		{
			m_renderer->UploadLights(m_project->GetScene());
		}
		m_project->MarkDirty();
		NEURUS_LOG("[Editor] Added point light at (3, 3, 3)");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add light: " << e.what());
	}
}

// --- Remaining stub handlers (implemented in later tasks) ---
void Editor::OnScreenshotRequested() { NEURUS_LOG("[Editor] OnScreenshotRequested stub"); }
void Editor::OnScreenshotAllRequested() { NEURUS_LOG("[Editor] OnScreenshotAllRequested stub"); }

void Editor::OnIBLLoad()
{
	Scene* scene = m_ownerScene;
	if (!scene)
	{
		NEURUS_ERR("[Editor] OnIBLLoad: no scene available");
		return;
	}

	// --- Find or create an Environment on the scene ---
	Environment::Resource env;
	if (!scene->env_list.empty())
	{
		env = scene->env_list.begin()->second;
		NEURUS_LOG("[Editor] Reusing existing environment (ID "
		           << env->GetObjectID() << ")");
	}
	else
	{
		env = std::make_shared<Environment>();
		scene->UseEnvironment(env);
		NEURUS_LOG("[Editor] Created new environment (ID "
		           << env->GetObjectID() << ")");
	}

	// Set the equirect path (resolved from resource directory)
	const std::string hdrPath =
		resolveResourcePath("tex/hdr/room.hdr").toStdString();
	env->SetEquirectPath(hdrPath);

	GenerateIBL(env);
}

void Editor::GenerateIBL(const std::shared_ptr<Environment>& env)
{
	if (!m_vkContext || !m_renderer)
	{
		NEURUS_ERR("[Editor] GenerateIBL: VulkanContext or Renderer not available");
		return;
	}
	auto* iblPass = m_renderer->GetIBLPass();
	if (!iblPass)
	{
		NEURUS_ERR("[Editor] GenerateIBL: IBLPass not available");
		return;
	}

	auto& device = m_vkContext->device();
	auto& pd = m_vkContext->physicalDevice();
	auto queue = m_vkContext->graphicsQueue();
	uint32_t qfi = m_vkContext->graphicsQueueFamily();

	// Ensure cubemaps exist (lazily created on first call)
	env->BuildIBLTextures(device, pd);

	// Load HDR or fallback
	auto equirect = Image::LoadFromPath(device, pd, queue, qfi, env->GetEquirectPath());
	if (!equirect)
	{
		equirect = Environment::GenerateFallbackImage(device, pd, queue, qfi);
	}

	// Generate IBL into cubemaps (owned by Environment, reused across updates)
	iblPass->Generate(*equirect, *env->GetCubemapDiffuse(), *env->GetCubemapSpecular());

	NEURUS_LOG("[Editor] IBL generated for environment (ID " << env->GetObjectID() << ")");
}

} // namespace neurus
