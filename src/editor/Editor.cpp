#include "Editor.h"

#include "editor/Context.h"
#include "editor/controllers/CameraController.h"
#include "editor/events/CameraEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/EventBus.h"
#include "editor/events/UIEvents.h"
#include "asset/Project.h"

#include "app/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "render/RenderCache.h"
#include "render/UploadManager.h"
#include "render/resources/LightGPU.h"
#include "render/resources/LightingGPU.h"
#include "render/resources/MeshGPU.h"

#include "core/Log.h"
#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Scene.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

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

Editor::Editor(VulkanContext* vkCtx, DeferredRenderer* renderer, EventQueue& eventBus)
	: ed_vkContext(vkCtx)
	, ed_renderer(renderer)
	, ed_eventBus(eventBus)
{
	if (ed_vkContext && ed_renderer)
	{
		ed_uploadManager = std::make_unique<UploadManager>(
			ed_vkContext->device(),
			ed_vkContext->physicalDevice(),
			ed_vkContext->graphicsQueueFamily());
	}
}

void Editor::SetProject(std::unique_ptr<neurus::project::Project> project)
{
	ed_project = std::move(project);
}

Editor::~Editor()
{
	// Destroy in order: Context (references scene) → Project (owns scene)
	ed_context.reset();
	ed_project.reset();
}

void Editor::Initialize(Scene& scene)
{
	// Store the scene reference for OnIBLLoad and other operations
	ed_ownerScene = &scene;

	// Note: Mesh/light GPU upload happens AFTER window is shown and surface
	// is ready — see UploadSceneResources() called from Application::Run()
	// and from OnProjectOpen() / OnProjectNew().

	// Create Context with Application-owned EventQueue reference
	ed_context = std::make_unique<Context>(ed_eventBus);
	ed_context->editor.SetScene(&scene);

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

	// Handle sun light add (Edit -> Add -> Sun Light)
	QObject::connect(&uiEvents, &neurus::UIEvents::sunLightAddRequested,
		[this]() { OnSunLightAdd(); });

	// Load IBL environment now that the scene is available
	OnIBLLoad();

	// --- Register controllers ---
	RegisterController<CameraController>(ed_eventBus);

	// --- Subscribe to EnvironmentChanged to regenerate IBL cubemaps on demand ---
	ed_eventBus.subscribe<EnvironmentChanged>([this](const EnvironmentChanged& e) {
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
	return ed_project->GetScene();
}

neurus::project::Project& Editor::GetProject()
{
	return *ed_project;
}

// --- Project signal handlers ---

void Editor::OnProjectNew()
{
	try
	{
		// Drain GPU work before destroying the old project's GPU resources.
		if (ed_renderer)
		{
			ed_renderer->WaitIdle();
		}

		ed_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::New());
		NEURUS_LOG("[Editor] Created new project.");

		// Update owner scene pointer
		auto& projectScene = ed_project->GetScene();
		ed_ownerScene = &projectScene;

		UploadSceneResources();

		if (ed_context)
		{
			ed_context->editor.SetScene(&projectScene);
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
		if (ed_renderer)
		{
			ed_renderer->WaitIdle();
		}

		ed_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::Open(path.toStdString(),
			                               resolveResourcePath("").toStdString()));
		NEURUS_LOG("[Editor] Opened project: " << path.toStdString());

		// Update owner scene pointer to the new project's scene
		auto& projectScene = ed_project->GetScene();
		ed_ownerScene = &projectScene;

		UploadSceneResources();

		if (ed_context)
		{
			ed_context->editor.SetScene(&projectScene);
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
	if (ed_project)
	{
		try { ed_project->Save(); }
		catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
	}
}

void Editor::OnProjectSaveAs(const QString& path)
{
	if (ed_project)
	{
		try { ed_project->Save(path.toStdString()); }
		catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
	}
}

// --- Mesh, Camera, Light signal handlers ---

void Editor::OnMeshImport(const QString& path)
{
	try {
		auto mesh = std::make_shared<neurus::Mesh>(path.toStdString());
		ed_project->GetScene().UseMesh(mesh);

		// Upload to GPU immediately via UploadManager
		if (ed_uploadManager && ed_renderer)
		{
			auto meshGPU = ed_uploadManager->UploadMesh(*mesh);
			ed_renderer->GetRenderCache().UseMeshGPU(mesh->GetObjectID(), std::move(meshGPU));
		}

		ed_project->MarkDirty();
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
		camera->SetCamPos(glm::vec3(0.0f, -5.0f, 2.0f));
		camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
		ed_project->GetScene().UseCamera(camera);
		ed_project->MarkDirty();
		NEURUS_LOG("[Editor] Added camera at (0, -5, 2)");
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
		ed_project->GetScene().UseLight(light);
		// Upload lighting via UploadManager (variant API) → RenderCache
		if (ed_uploadManager && ed_renderer)
		{
			auto lightDict = ed_uploadManager->UploadLighting(ed_project->GetScene().light_list);
			ed_renderer->GetRenderCache().UpdateLighting(lightDict);
		}
		// Upload shadow map for this light
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		ed_project->MarkDirty();
		NEURUS_LOG("[Editor] Added point light at (3, 3, 3)");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add light: " << e.what());
	}
}

void Editor::OnSunLightAdd()
{
	try {
		auto light = std::make_shared<neurus::Light>(
			neurus::SUNLIGHT, 5.0f, glm::vec3(1.0f, 0.95f, 0.8f));
		light->SetPosition(glm::vec3(0.0f, 0.0f, 10.0f));
		light->SetRotation(glm::vec3(-90.0f, 0.0f, 0.0f));
		light->use_shadow = true;
		ed_project->GetScene().UseLight(light);
		// Upload lighting via UploadManager (variant API) → RenderCache
		if (ed_uploadManager && ed_renderer)
		{
			auto lightDict = ed_uploadManager->UploadLighting(ed_project->GetScene().light_list);
			ed_renderer->GetRenderCache().UpdateLighting(lightDict);
		}
		// Upload shadow map for this sun light
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		ed_project->MarkDirty();
		NEURUS_LOG("[Editor] Added sun light at (0, 0, 10)");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add sun light: " << e.what());
	}
}

// --- Remaining stub handlers (implemented in later tasks) ---
void Editor::OnScreenshotRequested() { NEURUS_LOG("[Editor] OnScreenshotRequested stub"); }
void Editor::OnScreenshotAllRequested() { NEURUS_LOG("[Editor] OnScreenshotAllRequested stub"); }

void Editor::OnIBLLoad()
{
	Scene* scene = ed_ownerScene;
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
	if (!ed_uploadManager || !ed_renderer)
	{
		NEURUS_ERR("[Editor] GenerateIBL: UploadManager or Renderer not available");
		return;
	}

	auto envGPU = ed_uploadManager->UploadEnvironment(*env);
	ed_renderer->GetRenderCache().UseEnvironmentGPU(env->GetObjectID(), std::move(envGPU));

	NEURUS_LOG("[Editor] IBL generated for environment (ID " << env->GetObjectID() << ")");
}

// =========================================================================
// UploadSceneResources() – upload all meshes and shadow-casting lights to GPU
// =========================================================================

void Editor::UploadSceneResources()
{
	if (!ed_uploadManager || !ed_renderer || !ed_project) return;

	auto& scene = ed_project->GetScene();

	for (const auto& [id, mesh] : scene.mesh_list)
	{
		if (!mesh || !mesh->o_mesh) continue;
		const int objId = mesh->GetObjectID();
		NEURUS_LOG("[Editor::UploadSceneResources] Registering MeshGPU: mapKey=" << id << " GetObjectID=" << objId);
		if (ed_renderer->GetRenderCache().GetMeshGPU(objId)) continue;
		auto meshGPU = ed_uploadManager->UploadMesh(*mesh);
		ed_renderer->GetRenderCache().UseMeshGPU(objId, std::move(meshGPU));
	}

	for (const auto& [uid, light] : scene.light_list)
	{
		if (!light || !light->use_shadow) continue;
		if (ed_renderer->GetRenderCache().GetLightGPU(uid)) continue;
		auto lightGPU = ed_uploadManager->UploadLight(*light);
		ed_renderer->GetRenderCache().UseLightGPU(uid, std::move(lightGPU));
	}

	// Upload lighting SSBO (point/sun light structs) via variant API
	{
		auto lightDict = ed_uploadManager->UploadLighting(scene.light_list);
		ed_renderer->GetRenderCache().UpdateLighting(lightDict);
	}

	NEURUS_LOG("[Editor] Uploaded scene resources to GPU");
}

// =========================================================================
// HandleResize() – dispatch CameraResizeEvent via event bus
// =========================================================================

void Editor::HandleResize(uint32_t width, uint32_t height)
{
	auto* cam = GetScene().GetActiveCamera();
	if (!cam) return;

	ed_eventBus.enqueue(CameraResizeEvent{const_cast<Camera*>(cam),
	                                       static_cast<int>(width),
	                                       static_cast<int>(height)});
}

// =========================================================================
// Edit() – translate InputState → CameraEvents, following OpenGL Viewport.cpp pattern
// =========================================================================

void Editor::Edit(const InputState& input)
{
	auto& scene = GetScene();
	auto* cam = const_cast<Camera*>(scene.GetActiveCamera());
	if (!cam) return;

	// Translate InputState → CameraEvents (matching OpenGL Viewport.cpp:178-198)
	if (input.middleMouseHeld)
	{
		if (input.ctrlHeld)
			ed_eventBus.enqueue(CameraPushEvent{cam, input.mouseDeltaX, input.mouseDeltaY});
		else if (input.shiftHeld)
			ed_eventBus.enqueue(CameraSlideEvent{cam, input.mouseDeltaX, input.mouseDeltaY});
		else
			ed_eventBus.enqueue(CameraRotateEvent{cam, input.mouseDeltaX, input.mouseDeltaY});
	}
	if (std::abs(input.scrollDelta) > 0.001f)
		ed_eventBus.enqueue(CameraZoomEvent{cam, input.scrollDelta});

	// Process() is called once at the end of newFrame, not here.
}

} // namespace neurus
