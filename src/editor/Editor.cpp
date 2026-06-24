#include "Editor.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "editor/Context.h"
#include "editor/controllers/CameraController.h"
#include "editor/events/CameraEvents.h"
#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/UIEvents.h"
#include "project/Project.h"
#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Scene.h"
#include "render/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "render/Image.h"
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

	// --- Register controllers ---
	RegisterController<CameraController>(EventQueue());

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
		// Drain GPU work before destroying the old project's GPU resources.
		if (m_renderer)
		{
			m_renderer->WaitIdle();
		}

		m_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::New());
		NEURUS_LOG("[Editor] Created new project.");

		// Update owner scene pointer
		auto& projectScene = m_project->GetScene();
		m_ownerScene = &projectScene;

		// Upload scene data to GPU
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
			m_context->editor.SetScene(&projectScene);
		}

		// Generate IBL for the new environment
		OnIBLLoad();
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
		// Drain any GPU work referencing the old project's resources.
		if (m_renderer)
		{
			m_renderer->WaitIdle();
		}

		m_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::Open(path.toStdString(),
			                               resolveResourcePath("").toStdString()));
		NEURUS_LOG("[Editor] Opened project: " << path.toStdString());

		// Update owner scene pointer to the new project's scene
		auto& projectScene = m_project->GetScene();
		m_ownerScene = &projectScene;

		// Re-upload scene data to GPU
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
			m_context->editor.SetScene(&projectScene);
		}

		// Regenerate IBL for the new environment (BuildIBLTextures, load HDR,
		// run IBLPass convolution).
		OnIBLLoad();
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

	// IBL is only enabled when the project provides an environment
	if (scene->env_list.empty())
	{
		NEURUS_LOG("[Editor] No environment in scene — IBL disabled (black background)");
		return;
	}

	auto env = scene->env_list.begin()->second;
	NEURUS_LOG("[Editor] Using environment (ID " << env->GetObjectID() << ")");

	// Read the equirect path from the environment object (set by CreateDefault or deserialized)
	const std::string& envRelPath = env->GetEquirectPath();
	if (envRelPath.empty())
	{
		NEURUS_LOG("[Editor] No environment path set, using procedural fallback");
	}
	else
	{
		const std::string hdrPath = resolveResourcePath(envRelPath.c_str()).toStdString();
		NEURUS_LOG("[Editor] Loading environment: " << envRelPath);
		env->SetEquirectPath(hdrPath);
	}

	GenerateIBL(env);
}

void Editor::GenerateIBL(const std::shared_ptr<Environment>& env)
{
	if (!m_vkContext || !m_renderer)
	{
		NEURUS_ERR("[Editor] GenerateIBL: VulkanContext or Renderer not available");
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

	// Generate IBL into cubemaps via DeferredRenderer wrapper (respects layer isolation)
	m_renderer->GenerateIBL(*equirect, *env->GetCubemapDiffuse(), *env->GetCubemapSpecular());

	NEURUS_LOG("[Editor] IBL generated for environment (ID " << env->GetObjectID() << ")");
}

// =========================================================================
// Edit() – translate InputState → CameraEvents, following OpenGL Viewport.cpp pattern
// =========================================================================

void Editor::Edit(const InputState& input)
{
	auto& scene = GetScene();
	auto* cam = const_cast<Camera*>(scene.GetActiveCamera());
	if (!cam) return;

	auto& bus = EventQueue();

	// Translate InputState → CameraEvents (matching OpenGL Viewport.cpp:178-198)
	if (input.middleMouseHeld)
	{
		if (input.ctrlHeld)
			bus.enqueue(CameraPushEvent{cam, input.mouseDeltaX, input.mouseDeltaY});
		else if (input.shiftHeld)
			bus.enqueue(CameraSlideEvent{cam, input.mouseDeltaX, input.mouseDeltaY});
		else
			bus.enqueue(CameraRotateEvent{cam, input.mouseDeltaX, input.mouseDeltaY});
	}
	if (std::abs(input.scrollDelta) > 0.001f)
		bus.enqueue(CameraZoomEvent{cam, input.scrollDelta});

	bus.Process(); // Dispatch to CameraController event handlers
}

} // namespace neurus
