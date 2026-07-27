#include "Editor.h"

#include "editor/events/InputEvents.h"
#include "editor/events/ProjectEvents.h"
#include "editor/events/AssetEvents.h"
#include "editor/events/ConfigEvents.h"
#include "editor/events/CameraEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/ShaderEvents.h"

#include "editor/controllers/CameraController.h"
#include "editor/controllers/ShaderController.h"
#include "editor/events/EventBus.h"
#include "asset/Project.h"
#include "asset/SceneComponent.h"
#include "asset/ConfigComponent.h"

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

Editor::Editor(DeferredRenderer* renderer, UploadManager* uploadManager)
	: ed_renderer(renderer)
	, ed_uploadManager(uploadManager)
	, m_scene(std::make_unique<Scene>())
{}

Editor::~Editor()
{
	// m_scene and m_config will be destroyed by unique_ptr automatically.
}

void Editor::Initialize()
{
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
		m_config = e.config;
	});

	ed_eventBus.subscribe<PositionChanged>([this](const PositionChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.obj_list.find(e.objectId);
		if (it == scene.obj_list.end()) return;
		void* transformPtr = it->second->GetTransform();
		if (!transformPtr) return;
		static_cast<Transform3D*>(transformPtr)->SetPosition(glm::vec3(e.posX, e.posY, e.posZ));
		m_dirty = true;

		if (it->second->o_type == ObjectID::GOType::GO_LIGHT ||
			it->second->o_type == ObjectID::GOType::GO_POLYLIGHT)
		{
			UploadLighting();
		}
	});

	ed_eventBus.subscribe<RotationChanged>([this](const RotationChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.obj_list.find(e.objectId);
		if (it == scene.obj_list.end()) return;
		void* transformPtr = it->second->GetTransform();
		if (!transformPtr) return;
		static_cast<Transform3D*>(transformPtr)->SetRotation(glm::vec3(e.rotX, e.rotY, e.rotZ));
		m_dirty = true;

		if (it->second->o_type == ObjectID::GOType::GO_LIGHT ||
			it->second->o_type == ObjectID::GOType::GO_POLYLIGHT)
		{
			UploadLighting();
		}
	});

	ed_eventBus.subscribe<ScaleChanged>([this](const ScaleChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.obj_list.find(e.objectId);
		if (it == scene.obj_list.end()) return;
		void* transformPtr = it->second->GetTransform();
		if (!transformPtr) return;
		static_cast<Transform3D*>(transformPtr)->SetScale(glm::vec3(e.sclX, e.sclY, e.sclZ));
		m_dirty = true;

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
		auto& scene = *m_scene;
		auto it = scene.cam_list.find(e.objectId);
		if (it == scene.cam_list.end()) return;
		it->second->SetTarPos(glm::vec3(e.targetX, e.targetY, e.targetZ));
		m_dirty = true;
	});

	ed_eventBus.subscribe<CameraFovChanged>([this](const CameraFovChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.cam_list.find(e.objectId);
		if (it == scene.cam_list.end()) return;
		it->second->ChangeCamPersp(e.fov);
		m_dirty = true;
	});

	// --- Mesh property events ---

	ed_eventBus.subscribe<MeshShadowChanged>([this](const MeshShadowChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.mesh_list.find(e.objectId);
		if (it == scene.mesh_list.end()) return;
		it->second->EnableShadow(e.enabled);
		m_dirty = true;
	});

	ed_eventBus.subscribe<MeshMaterialChanged>([this](const MeshMaterialChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.mesh_list.find(e.objectId);
		if (it == scene.mesh_list.end()) return;
		it->second->EnableMaterial(e.enabled);
		m_dirty = true;
	});

	// --- Light property events ---

	ed_eventBus.subscribe<LightPowerChanged>([this](const LightPowerChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.light_list.find(e.objectId);
		if (it == scene.light_list.end()) return;
		it->second->SetPower(e.power);
		m_dirty = true;
		auto gpuStruct = ed_uploadManager->UploadLighting(*it->second);
		ed_renderer->GetRenderCache().UpdateLight(e.objectId, gpuStruct);
	});

	ed_eventBus.subscribe<LightRadiusChanged>([this](const LightRadiusChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.light_list.find(e.objectId);
		if (it == scene.light_list.end()) return;
		it->second->SetRadius(e.radius);
		m_dirty = true;
		auto gpuStruct = ed_uploadManager->UploadLighting(*it->second);
		ed_renderer->GetRenderCache().UpdateLight(e.objectId, gpuStruct);
	});

	ed_eventBus.subscribe<LightShadowChanged>([this](const LightShadowChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.light_list.find(e.objectId);
		if (it == scene.light_list.end()) return;
		it->second->SetShadow(e.enabled);
		m_dirty = true;
		UploadLighting();
	});

	// --- Environment property events ---

	ed_eventBus.subscribe<EnvironmentIntensityChanged>([this](const EnvironmentIntensityChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.env_list.find(e.objectId);
		if (it == scene.env_list.end()) return;
		it->second->SetIntensity(e.intensity);
		m_dirty = true;
	});

	ed_eventBus.subscribe<EnvironmentRotationChanged>([this](const EnvironmentRotationChanged& e) {
		auto& scene = *m_scene;
		auto it = scene.env_list.find(e.objectId);
		if (it == scene.env_list.end()) return;
		it->second->SetRotation(e.rotation);
		m_dirty = true;
	});

	// Load IBL environment now that the scene is available
	OnIBLLoad();

	// --- Register controllers ---
	RegisterController<CameraController>();
	RegisterController<ShaderController>(this, ed_renderer, ed_uploadManager);

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
	return *m_scene;
}

RenderContext Editor::GetRenderContext() const
{
	RenderContext ctx;
	ctx.scene = m_scene.get();
	ctx.config = &m_config;
	return ctx;
}

// =========================================================================
// GetUIContext – build UI context from Editor state
// =========================================================================

UIContext Editor::GetUIContext() const
{
	UIContext ctx;
	ctx.renderConfig = &m_config;
	ctx.scene = m_scene.get();
	return ctx;
}

// =========================================================================
// Project lifecycle
// =========================================================================

void Editor::CreateDefaultScene(const std::string& objPath)
{
	m_scene = std::make_unique<Scene>();
	m_config = RenderConfig{};

	auto camera = std::make_shared<Camera>();
	camera->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
	camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
	m_scene->UseCamera(camera);

	auto mesh = std::make_shared<Mesh>(objPath);
	m_scene->UseMesh(mesh);

	auto light = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
	light->SetRadius(0.05f);
	m_scene->UseLight(light);

	auto env = std::make_shared<Environment>();
	env->SetEquirectPath("tex/hdr/room.hdr");
	m_scene->UseEnvironment(env);

	m_projectPath.clear();
	m_dirty = true;
}

void Editor::LoadProject(const std::string& path, const std::string& assetDir)
{
	if (ed_renderer)
		ed_renderer->WaitIdle();

	m_scene = std::make_unique<Scene>();
	m_config = RenderConfig{};

	neurus::project::Project serializer;
	serializer.Register<neurus::project::SceneComponent>(*m_scene);
	serializer.Register<neurus::project::ConfigComponent>(m_config);
	serializer.Load(path);

	for (auto& [id, mesh] : m_scene->mesh_list)
		mesh->ReloadMeshData(assetDir);

	m_projectPath = path;
	m_dirty = false;
	UploadSceneResources();
	OnIBLLoad();
}

void Editor::SaveProject(const std::string& path)
{
	neurus::project::Project serializer;
	serializer.Register<neurus::project::SceneComponent>(*m_scene);
	serializer.Register<neurus::project::ConfigComponent>(m_config);
	serializer.Save(path);
	m_projectPath = path;
	m_dirty = false;
}

void Editor::SaveProject()
{
	if (m_projectPath.empty())
		throw std::runtime_error("No file path set. Use SaveProject(path) first.");
	SaveProject(m_projectPath);
}

// --- Project signal handlers ---

void Editor::OnProjectNew()
{
	try
	{
		// Drain GPU work before destroying the old scene's GPU resources.
		if (ed_renderer)
		{
			ed_renderer->WaitIdle();
		}

		m_scene = std::make_unique<Scene>();
		m_config = RenderConfig{};
		m_projectPath.clear();
		m_dirty = false;
		NEURUS_LOG("[Editor] Created new project.");

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
	try { LoadProject(path, m_assetDir); }
	catch (const std::exception& e) { NEURUS_ERR("Failed to open project: " << e.what()); }
}

void Editor::OnProjectSave()
{
	try { SaveProject(); }
	catch (const std::exception& e) { NEURUS_ERR("Failed to save project: " << e.what()); }
}

void Editor::OnProjectSaveAs(const std::string& path)
{
	try { SaveProject(path); }
	catch (const std::exception& e) { NEURUS_ERR("Failed to save project as: " << e.what()); }
}

// --- Mesh, Camera, Light signal handlers ---

void Editor::OnMeshImport(const std::string& path)
{
	try {
		auto mesh = std::make_shared<neurus::Mesh>(path);
		m_scene->UseMesh(mesh);

		// Upload to GPU immediately via UploadManager
		if (ed_uploadManager && ed_renderer)
		{
			auto meshGPU = ed_uploadManager->UploadMesh(*mesh);
			ed_renderer->GetRenderCache().UseMeshGPU(mesh->GetObjectID(), std::move(meshGPU));
		}

		m_dirty = true;
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
		m_scene->UseCamera(camera);
		m_dirty = true;
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
		m_scene->UseLight(light);
		// Upload lighting via UploadManager (variant API) → RenderCache
		UploadLighting();
		// Upload shadow map for this light
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		m_dirty = true;
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
		m_scene->UseLight(light);
		// Upload lighting via UploadManager (variant API) → RenderCache
		UploadLighting();
		// Upload shadow map for this sun light
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		m_dirty = true;
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
	Scene* scene = m_scene.get();
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

	auto envGPU = ed_uploadManager->UploadEnvironment(*env,
	    ed_renderer->GetGraphicsQueue(),
	    ed_renderer->GetGraphicsQueueFamily());
	ed_renderer->GetRenderCache().UseEnvironmentGPU(env->GetObjectID(), std::move(envGPU));

	NEURUS_LOG("[Editor] IBL generated for environment (ID " << env->GetObjectID() << ")");
}

// =========================================================================
// SelectObject – update scene.selections from outliner click
// =========================================================================

void Editor::SelectObject(int objectId, bool increment)
{
	auto& scene = *m_scene;

	// id=0 means background click — clear everything
	if (objectId == 0)
	{
		if (!increment)	scene.selections.ClearSelection();
		return;
	}

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
	auto& scene = *m_scene;

	auto it = scene.obj_list.find(objectId);
	if (it == scene.obj_list.end())
	{
		NEURUS_ERR("[Editor] ChangeObjectVisibility: object " << objectId << " not found");
		return;
	}

	it->second->SetVisible(viewportVisible, renderVisible);
	m_dirty = true;

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

	auto& scene = *m_scene;

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
		auto& scene = *m_scene;
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
