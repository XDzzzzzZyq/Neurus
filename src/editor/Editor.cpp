#include "Editor.h"

#include "editor/events/InputEvents.h"
#include "editor/events/AssetEvents.h"
#include "editor/events/OperationEvents.h"
#include "editor/events/ConfigEvents.h"
#include "editor/events/CameraEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/ShaderEvents.h"
#include "editor/events/SceneEvents.h"

#include "editor/controllers/CameraController.h"
#include "editor/controllers/RenderConfigController.h"
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
#include "render/shaders/RenderShader.h"
#include "render/shaders/ShaderLibrary.h"
#include "ui/UIContext.h"

#include "core/Log.h"
#include "asset/data/MeshData.h"
#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace neurus {

Editor::Editor(DeferredRenderer* renderer, UploadManager* uploadManager)
	: m_scene(std::make_unique<Scene>())
	, m_resources(std::make_unique<ResourceManager>())
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

	// Shader creation constructs a pooled RenderShader, so it is Editor-owned
	// (like mesh/light/camera adds). ShaderController keeps only pool-free
	// handlers (compile, code/struct edits, undo/redo replay).
	ed_eventBus.subscribe<ShaderCreateRequested>([this](const ShaderCreateRequested& e) {
		ed_eventBus.enqueue(RenderResetEvent{});
		OnCreateShader(e);
	});

	// Undo/redo replay their inverse events synchronously (never queued), so
	// the mutation applies in-place and cannot reorder against live input.
	ed_eventBus.subscribe<UndoRequested>([this](const UndoRequested&) {
		ed_operations.Undo();
	});
	ed_eventBus.subscribe<RedoRequested>([this](const RedoRequested&) {
		ed_operations.Redo();
	});

	// Load IBL environment now that the scene is available
	OnIBLLoad();

	// --- Register controllers ---
	RegisterController<CameraController>();
	RegisterController<ShaderController>();

	// SceneController needs the Editor-owned resource pool (UID -> object
	// resolution for add/delete membership), which RegisterController<T>()
	// can't supply — construct it manually with a provider.
	{
		auto sceneCtrl = std::make_unique<SceneController>(
			[this]() -> ResourceManager* { return m_resources.get(); });
		sceneCtrl->Init(ed_eventBus, ed_operations);
		ed_controllers.push_back(std::move(sceneCtrl));
	}

	// RenderConfigController needs the Editor-owned RenderConfig, which
	// RegisterController<T>() can't supply — construct it manually with a
	// provider. It applies + records config edits (the single mutation path),
	// replacing the inline RenderConfigChangedEvent handler removed above.
	{
		auto cfgCtrl = std::make_unique<RenderConfigController>(
			[this]() -> RenderConfig* { return &m_config; });
		cfgCtrl->Init(ed_eventBus, ed_operations);
		ed_controllers.push_back(std::move(cfgCtrl));
	}

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

	// Middle-button press/release bound the orbit/pan/dolly drag gesture so it
	// collapses to one undo entry. The typed drag events flow through the same
	// controller chain as the camera moves themselves (no direct handling here).
	ed_eventBus.subscribe<MousePressEvent>([this](const MousePressEvent& e) {
		if (e.button != Input::Middle) return;
		if (auto* cam = const_cast<Camera*>(GetScene().GetActiveCamera()))
			ed_eventBus.enqueue(CameraDragBegin{cam});
	});
	ed_eventBus.subscribe<MouseReleaseEvent>([this](const MouseReleaseEvent& e) {
		if (e.button != Input::Middle) return;
		if (auto* cam = const_cast<Camera*>(GetScene().GetActiveCamera()))
			ed_eventBus.enqueue(CameraDragEnd{cam});
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
	m_resources->Clear();
	m_config = RenderConfig{};

	auto camera = m_resources->Load<Camera>();
	camera->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
	camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
	m_scene->UseCamera(camera);

	auto meshData = m_resources->Load<MeshData>(objPath);
	auto mesh = m_resources->Load<Mesh>(meshData);
	m_scene->UseMesh(mesh);

	auto light = m_resources->Load<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
	light->SetRadius(0.01f);
	m_scene->UseLight(light);

	// Paths are relative ("res/...") so project files stay portable; the
	// resource layer resolves them against the asset dir at load time. The
	// pooled ImageData owns the path; the Environment only wraps it.
	auto imageData = m_resources->Load<ImageData>("res/tex/hdr/room.hdr");
	auto env = m_resources->Load<Environment>(imageData);
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
		m_resources->Clear(); // No stale pooled object may leak into a save.
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
	m_resources->Clear(); // Pool is restored from the project file next.
	m_config = RenderConfig{};
	ed_operations.Clear(); // History does not span scenes.
	// Application deserializes into GetScene()/GetRenderConfig() before FinishLoad().
}

void Editor::FinishLoad()
{
	// Meshes now resolve their pooled MeshData via Scene::ResolveDataReferences
	// (SceneComponent), so no path-based reload loop is needed here.
	m_dirty = false;
	UploadSceneResources();
	OnIBLLoad();
}


void Editor::OnMeshImport(const std::string& path)
{
	try {
		// Import = load the resource into the pool. The SceneController
		// registers it into the scene and records the undoable Add operation.
		auto meshData = m_resources->Load<MeshData>(path);
		auto mesh = m_resources->Load<Mesh>(meshData);

		// Pool-keyed GPU cache upload (RenderCache follows the pool, not the
		// scene — GeometryPass would also create the MeshGPU lazily).
		if (ed_uploadManager && ed_renderer)
		{
			auto meshGPU = ed_uploadManager->UploadMesh(*mesh);
			ed_renderer->GetRenderCache().UseMeshGPU(mesh->GetObjectID(), std::move(meshGPU));
		}

		ed_eventBus.enqueue(SceneObjectAddRequested{m_scene.get(), mesh->GetObjectID()});
		NEURUS_LOG("[Editor] Imported mesh: " << path);
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to import mesh: " << e.what());
	}
}

void Editor::OnCameraAdd()
{
	try {
		auto camera = m_resources->Load<Camera>();
		camera->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
		camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
		ed_eventBus.enqueue(SceneObjectAddRequested{m_scene.get(), camera->GetObjectID()});
		NEURUS_LOG("[Editor] Added camera at (0, -5, 2)");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add camera: " << e.what());
	}
}

void Editor::OnLightAdd()
{
	try {
		auto light = m_resources->Load<Light>(
			neurus::POINTLIGHT, 10.0f, glm::vec3(1.0f));
		light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
		light->SetRadius(0.01f);
		// Pool-keyed shadow-map upload; the light SSBO rebuild happens via
		// LightingRebuild after the controller registers the light in the scene.
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		ed_eventBus.enqueue(SceneObjectAddRequested{m_scene.get(), light->GetObjectID()});
		NEURUS_LOG("[Editor] Added point light at (3, 3, 3)");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add light: " << e.what());
	}
}

void Editor::OnSunLightAdd()
{
	try {
		auto light = m_resources->Load<Light>(
			neurus::SUNLIGHT, 5.0f, glm::vec3(1.0f, 0.95f, 0.8f));
		light->SetPosition(glm::vec3(0.0f, 0.0f, 10.0f));
		light->SetRotation(glm::vec3(-90.0f, 0.0f, 0.0f));
		light->use_shadow = true;
		// Pool-keyed shadow-map upload; SSBO rebuild via LightingRebuild
		// after the controller registers the light in the scene.
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		ed_eventBus.enqueue(SceneObjectAddRequested{m_scene.get(), light->GetObjectID()});
		NEURUS_LOG("[Editor] Added sun light at (0, 0, 10)");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add sun light: " << e.what());
	}
}

void Editor::OnSpotLightAdd()
{
	try {
		auto light = m_resources->Load<Light>(
			neurus::SPOTLIGHT, 30.0f, glm::vec3(1.0f, 0.75f, 0.4f));
		light->SetPosition(glm::vec3(0.0f, 0.0f, 6.0f));
		light->SetRotation(glm::vec3(-90.0f, 0.0f, 0.0f));
		light->SetRadius(0.01f);
		light->SetCutoff(0.95f);        // ~18° inner cone half-angle
		light->SetOuterCutoff(0.85f);   // ~32° outer cone half-angle
		light->use_shadow = true;
		// Pool-keyed shadow-map upload; SSBO rebuild via LightingRebuild
		// after the controller registers the light in the scene.
		if (ed_uploadManager && ed_renderer && light->use_shadow)
		{
			auto lightGPU = ed_uploadManager->UploadLight(*light);
			ed_renderer->GetRenderCache().UseLightGPU(light->GetObjectID(), std::move(lightGPU));
		}
		ed_eventBus.enqueue(SceneObjectAddRequested{m_scene.get(), light->GetObjectID()});
		NEURUS_LOG("[Editor] Added spot light at (0, 0, 6) pointing down");
	}
	catch (const std::exception& e) {
		NEURUS_ERR("Failed to add spot light: " << e.what());
	}
}

void Editor::OnCreateShader(const ShaderCreateRequested& e)
{
	auto* mesh = Mesh::As(e.object);
	if (!mesh)
	{
		NEURUS_ERR("[Editor] OnCreateShader: not a mesh");
		return;
	}
	if (mesh->o_shader)
	{
		NEURUS_LOG("[Editor] Mesh already has a shader");
		return;
	}

	const int objectId = mesh->GetObjectID();
	const std::string shaderName = "MeshShader_" + std::to_string(objectId);

	try
	{
		// Load<T> constructs + registers the RenderShader (pooled UID); the
		// shader owns its own parsing, so parse explicitly here. Paths use the
		// "res/..." prefix (resolved from the working directory by the shader
		// pipeline), matching ShaderLibrary-created shaders.
		auto shader = m_resources->Load<RenderShader>(
			shaderName, "res/shaders/render/gbuffer.vert", "res/shaders/render/gbuffer.frag");
		if (!shader->ParseAndGenerate())
		{
			NEURUS_ERR("[Editor] Failed to create default shader for mesh " << objectId);
			m_resources->Remove(shader->GetObjectID());
			return;
		}

		mesh->SetObjShader(shader); // o_shader + o_shaderId (pooled reference)

		// Compile both stages to SPIR-V and bump version on success.
		auto& s = *mesh->o_shader;
		bool allOk = true;
		if (s.HasStage(ShaderType::VERTEX))
		{
			auto& unit = s.GetStage(ShaderType::VERTEX);
			unit.spv = ShaderLibrary::Compile(unit, ShaderType::VERTEX, s.GetName());
			if (unit.spv.empty()) { allOk = false; }
			else { unit.BumpVersion(); }
		}
		if (s.HasStage(ShaderType::FRAGMENT))
		{
			auto& unit = s.GetStage(ShaderType::FRAGMENT);
			unit.spv = ShaderLibrary::Compile(unit, ShaderType::FRAGMENT, s.GetName());
			if (unit.spv.empty()) { allOk = false; }
			else { unit.BumpVersion(); }
		}
		if (allOk)
			s.BumpVersion();

		NEURUS_LOG("[Editor] Created shader for mesh " << objectId << ": " << shaderName);
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[Editor] Exception creating shader: " << ex.what());
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

	// The environment wraps a pooled ImageData (path owned by the data layer).
	// If the pooled pixels are unavailable (missing source file), fall back to
	// the procedural gradient inside UploadEnvironment - no path loading here.
	if (!env->GetEquirectData() || !env->GetEquirectData()->IsValid())
	{
		NEURUS_LOG("[Editor] Environment has no valid equirect data, using procedural fallback");
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

	auto& cache = ed_renderer->GetRenderCache();

	// The UID-keyed GPU caches (MeshGPU, LightGPU) mirror the POOL, not the
	// scene: pooled objects stay referenced by undo history after deletion,
	// so their GPU resources must exist for undo/redo to render them without
	// re-uploading — even after a project save/load rebuilt the cache.
	m_resources->ForEach<Mesh>([&](const std::shared_ptr<Mesh>& mesh) {
		if (!mesh || !mesh->o_mesh) return;
		const int objId = mesh->GetObjectID();
		NEURUS_LOG("[Editor::UploadSceneResources] Registering MeshGPU: GetObjectID=" << objId);
		if (cache.GetMeshGPU(objId)) return;
		auto meshGPU = ed_uploadManager->UploadMesh(*mesh);
		cache.UseMeshGPU(objId, std::move(meshGPU));
	});

	m_resources->ForEach<Light>([&](const std::shared_ptr<Light>& light) {
		if (!light || !light->use_shadow) return;
		const int uid = light->GetObjectID();
		if (cache.GetLightGPU(uid)) return;
		auto lightGPU = ed_uploadManager->UploadLight(*light);
		cache.UseLightGPU(uid, std::move(lightGPU));
	});

	// The light SSBO remains a scene projection (built from scene->light_list).
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
