#include "Editor.h"

#include "editor/events/InputEvents.h"
#include "editor/events/ProjectEvents.h"
#include "editor/events/AssetEvents.h"
#include "editor/events/ConfigEvents.h"
#include "editor/events/CameraEvents.h"
#include "editor/events/EditorEvents.h"

#include "editor/controllers/CameraController.h"
#include "editor/events/EventBus.h"
#include "asset/Project.h"

#include "app/VulkanContext.h"
#include "render/DeferredRenderer.h"
#include "render/RenderCache.h"
#include "render/UploadManager.h"
#include "render/resources/LightGPU.h"
#include "render/resources/LightingGPU.h"
#include "render/resources/MeshGPU.h"

#include "render/RenderContext.h"
#include "ui/UIContext.h"

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

Editor::Editor(VulkanContext* vkCtx, DeferredRenderer* renderer)
	: ed_vkContext(vkCtx)
	, ed_renderer(renderer)
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
	ed_project.reset();
}

void Editor::Initialize(Scene& scene)
{
	// Store the scene reference for OnIBLLoad and other operations
	ed_ownerScene = &scene;

	// Note: Mesh/light GPU upload happens AFTER window is shown and surface
	// is ready — see UploadSceneResources() called from Application::Run()
	// and from OnProjectOpen() / OnProjectNew().

	// --- Wire project file signal handlers ---
	// (events are enqueued by Editor::OnUIEvent from UIEvents Qt signals)

	ed_eventBus.subscribe<ProjectNewEvent>([this](const ProjectNewEvent&) { OnProjectNew(); });
	ed_eventBus.subscribe<ProjectOpenEvent>([this](const ProjectOpenEvent& e) { OnProjectOpen(e.path); });
	ed_eventBus.subscribe<ProjectSaveEvent>([this](const ProjectSaveEvent&) { OnProjectSave(); });
	ed_eventBus.subscribe<ProjectSaveAsEvent>([this](const ProjectSaveAsEvent& e) { OnProjectSaveAs(e.path); });

	ed_eventBus.subscribe<MeshImportEvent>([this](const MeshImportEvent& e) { OnMeshImport(e.path); });
	ed_eventBus.subscribe<CameraAddEvent>([this](const CameraAddEvent&) { OnCameraAdd(); });
	ed_eventBus.subscribe<LightAddEvent>([this](const LightAddEvent&) { OnLightAdd(); });
	ed_eventBus.subscribe<SunLightAddEvent>([this](const SunLightAddEvent&) { OnSunLightAdd(); });

	ed_eventBus.subscribe<RenderConfigChangedEvent>([this](const RenderConfigChangedEvent& e) {
		ed_project->GetRenderConfig() = e.config;
	});

	ed_eventBus.subscribe<PositionChanged>([this](const PositionChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.obj_list.find(e.objectId);
		if (it == scene.obj_list.end()) return;
		void* transformPtr = it->second->GetTransform();
		if (!transformPtr) return;
		static_cast<Transform3D*>(transformPtr)->SetPosition(glm::vec3(e.posX, e.posY, e.posZ));
		ed_project->MarkDirty();

		if (it->second->o_type == ObjectID::GOType::GO_LIGHT ||
			it->second->o_type == ObjectID::GOType::GO_POLYLIGHT)
		{
			UploadLighting();
		}
	});

	ed_eventBus.subscribe<RotationChanged>([this](const RotationChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.obj_list.find(e.objectId);
		if (it == scene.obj_list.end()) return;
		void* transformPtr = it->second->GetTransform();
		if (!transformPtr) return;
		static_cast<Transform3D*>(transformPtr)->SetRotation(glm::vec3(e.rotX, e.rotY, e.rotZ));
		ed_project->MarkDirty();

		if (it->second->o_type == ObjectID::GOType::GO_LIGHT ||
			it->second->o_type == ObjectID::GOType::GO_POLYLIGHT)
		{
			UploadLighting();
		}
	});

	ed_eventBus.subscribe<ScaleChanged>([this](const ScaleChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.obj_list.find(e.objectId);
		if (it == scene.obj_list.end()) return;
		void* transformPtr = it->second->GetTransform();
		if (!transformPtr) return;
		static_cast<Transform3D*>(transformPtr)->SetScale(glm::vec3(e.sclX, e.sclY, e.sclZ));
		ed_project->MarkDirty();

		if (it->second->o_type == ObjectID::GOType::GO_LIGHT ||
			it->second->o_type == ObjectID::GOType::GO_POLYLIGHT)
		{
			UploadLighting();
		}
	});

	ed_eventBus.subscribe<ObjectSelected>([this](const ObjectSelected& e) {
		bool shiftOrCtrl = (e.modifiers & (Input::Mod_Shift | Input::Mod_Ctrl)) != 0;
		SelectObject(e.objectId, shiftOrCtrl);
	});

	ed_eventBus.subscribe<VisibilityChanged>([this](const VisibilityChanged& e) {
		ChangeObjectVisibility(e.objectId, e.viewportVisible, e.renderVisible);
	});

	// --- Camera property events ---

	ed_eventBus.subscribe<CameraTargetChanged>([this](const CameraTargetChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.cam_list.find(e.objectId);
		if (it == scene.cam_list.end()) return;
		it->second->SetTarPos(glm::vec3(e.targetX, e.targetY, e.targetZ));
		ed_project->MarkDirty();
	});

	ed_eventBus.subscribe<CameraFovChanged>([this](const CameraFovChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.cam_list.find(e.objectId);
		if (it == scene.cam_list.end()) return;
		it->second->ChangeCamPersp(e.fov);
		ed_project->MarkDirty();
	});

	// --- Mesh property events ---

	ed_eventBus.subscribe<MeshShadowChanged>([this](const MeshShadowChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.mesh_list.find(e.objectId);
		if (it == scene.mesh_list.end()) return;
		it->second->EnableShadow(e.enabled);
		ed_project->MarkDirty();
	});

	ed_eventBus.subscribe<MeshMaterialChanged>([this](const MeshMaterialChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.mesh_list.find(e.objectId);
		if (it == scene.mesh_list.end()) return;
		it->second->EnableMaterial(e.enabled);
		ed_project->MarkDirty();
	});

	// --- Light property events ---

	ed_eventBus.subscribe<LightPowerChanged>([this](const LightPowerChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.light_list.find(e.objectId);
		if (it == scene.light_list.end()) return;
		it->second->SetPower(e.power);
		ed_project->MarkDirty();
		UploadLighting();
	});

	ed_eventBus.subscribe<LightRadiusChanged>([this](const LightRadiusChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.light_list.find(e.objectId);
		if (it == scene.light_list.end()) return;
		it->second->SetRadius(e.radius);
		ed_project->MarkDirty();
		UploadLighting();
	});

	ed_eventBus.subscribe<LightShadowChanged>([this](const LightShadowChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.light_list.find(e.objectId);
		if (it == scene.light_list.end()) return;
		it->second->SetShadow(e.enabled);
		ed_project->MarkDirty();
		UploadLighting();
	});

	// --- Environment property events ---

	ed_eventBus.subscribe<EnvironmentIntensityChanged>([this](const EnvironmentIntensityChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.env_list.find(e.objectId);
		if (it == scene.env_list.end()) return;
		it->second->SetIntensity(e.intensity);
		ed_project->MarkDirty();
	});

	ed_eventBus.subscribe<EnvironmentRotationChanged>([this](const EnvironmentRotationChanged& e) {
		auto& scene = ed_project->GetScene();
		auto it = scene.env_list.find(e.objectId);
		if (it == scene.env_list.end()) return;
		it->second->SetRotation(e.rotation);
		ed_project->MarkDirty();
	});

	// Load IBL environment now that the scene is available
	OnIBLLoad();

	// --- Register controllers ---
	RegisterController<CameraController>();

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

	ed_eventBus.subscribe<MouseMoveEvent>([this](const MouseMoveEvent& e) {
		auto* cam = const_cast<Camera*>(GetScene().GetActiveCamera());
		if (!cam) return;

		if (e.middleHeld)
		{
			if (e.modifiers & Input::Mod_Ctrl)
				ed_eventBus.enqueue(CameraPushEvent{cam, e.delta.x, e.delta.y});
			else if (e.modifiers & Input::Mod_Shift)
				ed_eventBus.enqueue(CameraSlideEvent{cam, e.delta.x, e.delta.y});
			else
				ed_eventBus.enqueue(CameraRotateEvent{cam, e.delta.x, e.delta.y});
		}
	});

	ed_eventBus.subscribe<MouseScrollEvent>([this](const MouseScrollEvent& e) {
		auto* cam = const_cast<Camera*>(GetScene().GetActiveCamera());
		if (!cam) return;

		if (std::abs(e.delta) > 0.001f)
			ed_eventBus.enqueue(CameraZoomEvent{cam, e.delta});
	});

	NEURUS_LOG("[Editor] Initialized");
}

Scene& Editor::GetScene()
{
	return ed_project->GetScene();
}

RenderContext Editor::GetRenderContext() const
{
	RenderContext ctx;
	ctx.scene = &ed_project->GetScene();
	ctx.config = &ed_project->GetRenderConfig();
	return ctx;
}

neurus::project::Project& Editor::GetProject()
{
	return *ed_project;
}

// =========================================================================
// GetUIContext – build UI context from Editor/Project state
// =========================================================================

UIContext Editor::GetUIContext() const
{
	UIContext ctx;
	ctx.renderConfig = &ed_project->GetRenderConfig();
	ctx.scene = &ed_project->GetScene();
	return ctx;
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

		// Generate IBL for the new environment
		OnIBLLoad();
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Failed to create new project: " << e.what());
	}
}

void Editor::OnProjectOpen(const std::string& path)
{
	try
	{
		// Drain any GPU work referencing the old project's resources.
		if (ed_renderer)
		{
			ed_renderer->WaitIdle();
		}

		ed_project = std::make_unique<neurus::project::Project>(
			neurus::project::Project::Open(path,
			                               resolveResourcePath("").toStdString()));
		NEURUS_LOG("[Editor] Opened project: " << path);

		// Update owner scene pointer to the new project's scene
		auto& projectScene = ed_project->GetScene();
		ed_ownerScene = &projectScene;

		UploadSceneResources();

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
	try { ed_project->Save(); }
	catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
}

void Editor::OnProjectSaveAs(const std::string& path)
{
	try { ed_project->Save(path); }
	catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
}

// --- Mesh, Camera, Light signal handlers ---

void Editor::OnMeshImport(const std::string& path)
{
	try {
		auto mesh = std::make_shared<neurus::Mesh>(path);
		ed_project->GetScene().UseMesh(mesh);

		// Upload to GPU immediately via UploadManager
		if (ed_uploadManager && ed_renderer)
		{
			auto meshGPU = ed_uploadManager->UploadMesh(*mesh);
			ed_renderer->GetRenderCache().UseMeshGPU(mesh->GetObjectID(), std::move(meshGPU));
		}

		ed_project->MarkDirty();
		NEURUS_LOG("[Editor] Imported mesh: " << path);
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to import mesh: " << e.what());
	}
}

void Editor::OnCameraAdd()
{
	try {
		auto camera = std::make_shared<neurus::Camera>();
		camera->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
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
		UploadLighting();
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
		UploadLighting();
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
// SelectObject – update scene.selections from outliner click
// =========================================================================

void Editor::SelectObject(int objectId, bool increment)
{
	auto& scene = ed_project->GetScene();

	auto it = scene.obj_list.find(objectId);
	if (it == scene.obj_list.end())
	{
		NEURUS_ERR("[Editor] SelectObject: object " << objectId << " not found in obj_list");
		return;
	}

	auto* objPtr = it->second.get();
	scene.selections.Select(objPtr, increment);

	NEURUS_LOG("[Editor] SelectObject: id=" << objectId
	           << " increment=" << increment
	           << " count=" << scene.selections.GetSelectionCount());
}

// =========================================================================
// ChangeObjectVisibility – propagate outliner toggle to scene object
// =========================================================================

void Editor::ChangeObjectVisibility(int objectId, bool viewportVisible, bool renderVisible)
{
	auto& scene = ed_project->GetScene();

	auto it = scene.obj_list.find(objectId);
	if (it == scene.obj_list.end())
	{
		NEURUS_ERR("[Editor] ChangeObjectVisibility: object " << objectId << " not found");
		return;
	}

	it->second->SetVisible(viewportVisible, renderVisible);
	ed_project->MarkDirty();

	// Light visibility change → re-upload lighting SSBO to reflect new state.
	// Shader variants (point/sun) are filtered by UploadLighting based on
	// is_viewport/is_rendered, so re-uploading the full light_list propagates
	// the toggle to GPU-side light arrays.
	if (it->second->o_type == ObjectID::GOType::GO_LIGHT ||
	    it->second->o_type == ObjectID::GOType::GO_POLYLIGHT)
	{
		UploadLighting();
	}
}

void Editor::UploadSceneResources()
{
	if (!ed_uploadManager || !ed_renderer) return;

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
	UploadLighting();

	NEURUS_LOG("[Editor] Uploaded scene resources to GPU");
}

void Editor::UploadLighting()
{
	if (ed_uploadManager && ed_renderer)
	{
		auto& scene = ed_project->GetScene();
		auto lightDict = ed_uploadManager->UploadLighting(scene.light_list);
		ed_renderer->GetRenderCache().UpdateLighting(lightDict);
	}
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
// Edit() — process all enqueued events (called from newFrame)
// =========================================================================

void Editor::Edit()
{
	ed_eventBus.Process();
}

} // namespace neurus
