#include "Editor.h"

#include "editor/events/InputEvents.h"
#include "editor/events/AssetEvents.h"
#include "editor/events/ConfigEvents.h"
#include "editor/events/CameraEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/ShaderEvents.h"
#include "editor/events/SceneEvents.h"

#include "editor/controllers/CameraController.h"
#include "editor/controllers/ShaderController.h"
#include "editor/controllers/SceneController.h"
#include "editor/events/EventBus.h"

#include "render/DeferredRenderer.h"
#include "render/RenderCache.h"
#include "render/UploadManager.h"
#include "render/resources/LightGPU.h"
#include "render/resources/LightingCache.h"
#include "render/resources/MeshGPU.h"

#include "render/RenderContext.h"
#include "ui/UIContext.h"

#include "core/Log.h"
#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
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
	: m_scene(std::make_unique<Scene>())
	, ed_operations(ed_eventBus, [this]() -> Scene* { return m_scene.get(); })
	, ed_renderer(renderer)
	, ed_uploadManager(uploadManager)
{}

Editor::~Editor()
{
	// m_scene and m_config will be destroyed by unique_ptr automatically.
}

void Editor::Initialize()
{
	// Note: Mesh/light GPU upload happens AFTER window is shown and surface
	// is ready — see UploadSceneResources() called from Application::Run()
	// and from the scene load lifecycle (BeginLoad/FinishLoad, NewScene).

	ed_eventBus.subscribe<MeshImportEvent>([this](const MeshImportEvent& e) {
		ed_eventBus.enqueue(RenderResetEvent{});
		OnMeshImport(e.path);
	});
	ed_eventBus.subscribe<CameraAddEvent>([this](const CameraAddEvent&) {
		ed_eventBus.enqueue(RenderResetEvent{});
		OnCameraAdd();
	});
	ed_eventBus.subscribe<LightAddEvent>([this](const LightAddEvent&) {
		ed_eventBus.enqueue(RenderResetEvent{});
		OnLightAdd();
	});
	ed_eventBus.subscribe<SunLightAddEvent>([this](const SunLightAddEvent&) {
		ed_eventBus.enqueue(RenderResetEvent{});
		OnSunLightAdd();
	});
	ed_eventBus.subscribe<SpotLightAddEvent>([this](const SpotLightAddEvent&) {
		ed_eventBus.enqueue(RenderResetEvent{});
		OnSpotLightAdd();
	});

	// Undo/redo replay their inverse events synchronously (never queued), so
	// the mutation applies in-place and cannot reorder against live input.
	ed_eventBus.subscribe<UndoRequested>([this](const UndoRequested&) {
		ed_operations.Undo();
	});
	ed_eventBus.subscribe<RedoRequested>([this](const RedoRequested&) {
		ed_operations.Redo();
	});

	ed_eventBus.subscribe<RenderConfigChangedEvent>([this](const RenderConfigChangedEvent& e) {
		ed_eventBus.enqueue(RenderResetEvent{});
		m_config = e.config;
	});

	// Load IBL environment now that the scene is available
	OnIBLLoad();

	// --- Register controllers ---
	RegisterController<CameraController>();
	RegisterController<ShaderController>();
	RegisterController<SceneController>();

	// --- Subscribe to EnvironmentChanged to regenerate IBL cubemaps on demand ---
	ed_eventBus.subscribe<EnvironmentChanged>([this](const EnvironmentChanged& e) {
		ed_eventBus.enqueue(RenderResetEvent{});
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

	// --- Subscribe to RenderResetEvent to reset temporal accumulation ---
	ed_eventBus.subscribe<RenderResetEvent>([this](const RenderResetEvent&) {
		if (ed_renderer)
			ed_renderer->ResetShadowAccumulation();
	});

	// --- SceneController GPU-sync + dirty subscriptions ---
	ed_eventBus.subscribe<SceneModified>([this](const SceneModified&) {
		m_dirty = true;
	});

	ed_eventBus.subscribe<LightGpuChanged>([this](const LightGpuChanged& e) {
		auto* light = Light::As(e.object);
		if (!light) return;
		auto gpuStruct = ed_uploadManager->UploadLighting(*light);
		ed_renderer->GetRenderCache().UpdateLight(e.object->GetObjectID(), gpuStruct);
	});

	ed_eventBus.subscribe<LightingRebuild>([this](const LightingRebuild&) {
		UploadLighting();
	});

	NEURUS_LOG("[Editor] Initialized");
}

Scene& Editor::GetScene()
{
	return *m_scene;
}

// =========================================================================
// GetContext – shared editor state (scene + config) for Render/UI contexts
// =========================================================================

EditorContext Editor::GetContext() const
{
	EditorContext ctx;
	ctx.scene = m_scene.get();
	ctx.config = &m_config;
	return ctx;
}

// =========================================================================
// Scene lifecycle
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
	light->SetRadius(0.01f);
	m_scene->UseLight(light);

	auto env = std::make_shared<Environment>();
	env->SetEquirectPath("tex/hdr/room.hdr");
	m_scene->UseEnvironment(env);

	m_dirty = true;
}

void Editor::NewScene()
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
		m_dirty = false;
		ed_operations.Clear(); // History does not span scenes.
		NEURUS_LOG("[Editor] Created new scene.");

		UploadSceneResources();

		// Generate IBL for the new environment
		OnIBLLoad();
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("Failed to create new scene: " << e.what());
	}
}

void Editor::BeginLoad()
{
	// Drain GPU work before destroying the old scene's GPU resources.
	if (ed_renderer)
		ed_renderer->WaitIdle();

	m_scene = std::make_unique<Scene>();
	m_config = RenderConfig{};
	ed_operations.Clear(); // History does not span scenes.
	// Application deserializes into GetScene()/GetRenderConfig() before FinishLoad().
}

void Editor::FinishLoad()
{
	for (auto& [id, mesh] : m_scene->mesh_list)
		mesh->ReloadMeshData(m_assetDir);

	m_dirty = false;
	UploadSceneResources();
	OnIBLLoad();
}


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
		light->SetRadius(0.01f);
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

void Editor::OnSpotLightAdd()
{
	try {
		auto light = std::make_shared<neurus::Light>(
			neurus::SPOTLIGHT, 30.0f, glm::vec3(1.0f, 0.75f, 0.4f));
		light->SetPosition(glm::vec3(0.0f, 0.0f, 6.0f));
		light->SetRotation(glm::vec3(-90.0f, 0.0f, 0.0f));
		light->SetRadius(0.01f);
		light->SetCutoff(0.95f);        // ~18° inner cone half-angle
		light->SetOuterCutoff(0.85f);   // ~32° outer cone half-angle
		light->use_shadow = true;
		m_scene->UseLight(light);
		// Upload lighting via UploadManager (variant API) → RenderCache
		UploadLighting();
		// Upload shadow map (cubemap, shared point-light pool)
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		m_dirty = true;
		NEURUS_LOG("[Editor] Added spot light at (0, 0, 6) pointing down");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add spot light: " << e.what());
	}
}

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
