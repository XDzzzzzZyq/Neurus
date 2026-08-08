/**
 * @file SceneController.cpp
 * @brief Event-driven scene mutation handlers (selection, transforms, props).
 *
 * Stateless -- all handlers are free functions in an anonymous namespace.
 * Each handler receives a discrete scene event, extracts the const ObjectID*
 * pointer, casts it to the concrete object type (Mesh*, Light*, Camera*, ...),
 * and applies the mutation. Scene-owned state (selection) uses the const UID*
 * cast to Scene*.
 *
 * GPU uploads are delegated to Editor via EditorEvents:
 *   - LightGpuChanged{object} -> single light SSBO struct update
 *   - LightingRebuild{}       -> full light SSBO dict rebuild
 *   - SceneModified{}         -> mark project dirty
 * Every mutation also enqueues RenderResetEvent{} (temporal accumulation).
 */

#include "editor/controllers/SceneController.h"

#include <vector>

#include "editor/events/SceneEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/operations/IOperationSink.h"
#include "editor/operations/SceneOperations.h"
#include "editor/Input.h"

#include "core/ResourceManager.h"

#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "scene/Transform.h"
#include "scene/ObjectID.h"

#include "core/Log.h"

namespace {

// ---------------------------------------------------------------------------
// Emit helpers
// ---------------------------------------------------------------------------

/** @brief Scene mutation: mark dirty + reset temporal accumulation. */
void Mutated(neurus::EventQueue& bus)
{
	bus.enqueue(neurus::SceneModified{});
	bus.enqueue(neurus::RenderResetEvent{});
}

/** @brief Single-light SSBO struct change. */
void LightStructChanged(const neurus::ObjectID* object, neurus::EventQueue& bus)
{
	bus.enqueue(neurus::LightGpuChanged{object});
	Mutated(bus);
}

/** @brief Full light SSBO dict rebuild. */
void LightingRebuilt(neurus::EventQueue& bus)
{
	bus.enqueue(neurus::LightingRebuild{});
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

/** @brief Captures the current selection set as an absolute UID endpoint. */
neurus::SelectionState SnapshotSelection(const neurus::Scene& scene)
{
	neurus::SelectionState state;
	neurus::SnapshotSelectionUids(scene.selections, state.selectedUids, state.activeUid);
	return state;
}

/** @brief Records a selection edit if it changed the set (keeps redo intact). */
void RecordSelection(neurus::Scene& scene, neurus::SelectionState before, neurus::IOperationSink& ops)
{
	neurus::SelectionState after = SnapshotSelection(scene);
	if (before.selectedUids == after.selectedUids && before.activeUid == after.activeUid)
		return; // No-op edit (e.g. re-select active object): nothing to record.
	ops.Submit(std::make_unique<neurus::SetSelectionOp>(std::move(before), std::move(after)));
}

void OnObjectSelected(const neurus::ObjectSelected& e, neurus::EventQueue&, neurus::IOperationSink& ops)
{
	neurus::Scene* scene = neurus::Scene::As(e.scene);
	if (!scene) return;

	const bool increment = (e.modifiers & (neurus::Input::Mod_Shift | neurus::Input::Mod_Ctrl)) != 0;

	neurus::SelectionState before = SnapshotSelection(*scene);

	if (!e.object)
	{
		// Background click (objectId 0) -> clear selection
		if (!increment) scene->selections.ClearSelection();
	}
	else
	{
		scene->selections.Select(e.object, increment);
	}

	RecordSelection(*scene, std::move(before), ops);
}

void OnObjectDeselected(const neurus::ObjectDeselected& e, neurus::EventQueue&, neurus::IOperationSink& ops)
{
	neurus::Scene* scene = neurus::Scene::As(e.scene);
	if (!scene || !e.object) return;

	neurus::SelectionState before = SnapshotSelection(*scene);
	scene->selections.Deselect(e.object, false);
	RecordSelection(*scene, std::move(before), ops);
}

void OnSelectionChanged(const neurus::SelectionChanged& e, neurus::EventQueue&)
{
	neurus::Scene* scene = neurus::Scene::As(e.scene);
	if (!scene) return;

	// Replay path for SetSelectionOp: resolve stored UIDs to live objects and
	// restore the whole set at once. Skip stale UIDs (object gone).
	neurus::RestoreSelectionUids(*scene, e.selectedUids, e.activeUid);
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------

void OnVisibilityChanged(const neurus::VisibilityChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;

	const neurus::VisibilityState before{ obj->is_viewport, obj->is_rendered };
	obj->SetVisible(e.viewportVisible, e.renderVisible);
	ops.Submit(std::make_unique<neurus::SetVisibilityOp>(
		obj->GetObjectID(), before, neurus::VisibilityState{ e.viewportVisible, e.renderVisible }));

	if (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT ||
	    obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT)
	{
		// Shader variants (point/sun) are filtered by UploadLighting based on
		// visibility, so the full dict must be rebuilt.
		LightingRebuilt(bus);
		return;
	}
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

void OnPositionChanged(const neurus::PositionChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;
	void* transformPtr = obj->GetTransform();
	if (!transformPtr) return;
	auto* transform = static_cast<neurus::Transform3D*>(transformPtr);

	const glm::vec3 before = transform->GetPosition();
	const glm::vec3 after(e.posX, e.posY, e.posZ);
	transform->SetPosition(after);
	ops.Submit(std::make_unique<neurus::SetPositionOp>(obj->GetObjectID(), before, after));

	if (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT ||
	    obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT)
		LightingRebuilt(bus);
	else
		Mutated(bus);
}

void OnRotationChanged(const neurus::RotationChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;
	void* transformPtr = obj->GetTransform();
	if (!transformPtr) return;
	auto* transform = static_cast<neurus::Transform3D*>(transformPtr);

	const glm::vec3 before = transform->GetRotation();
	const glm::vec3 after(e.rotX, e.rotY, e.rotZ);
	transform->SetRotation(after);
	ops.Submit(std::make_unique<neurus::SetRotationOp>(obj->GetObjectID(), before, after));

	if (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT ||
	    obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT)
		LightingRebuilt(bus);
	else
		Mutated(bus);
}

void OnScaleChanged(const neurus::ScaleChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;
	void* transformPtr = obj->GetTransform();
	if (!transformPtr) return;
	auto* transform = static_cast<neurus::Transform3D*>(transformPtr);

	const glm::vec3 before = transform->GetScale();
	const glm::vec3 after(e.sclX, e.sclY, e.sclZ);
	transform->SetScale(after);
	ops.Submit(std::make_unique<neurus::SetScaleOp>(obj->GetObjectID(), before, after));

	if (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT ||
	    obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT)
		LightingRebuilt(bus);
	else
		Mutated(bus);
}

// ---------------------------------------------------------------------------
// Camera properties
// ---------------------------------------------------------------------------

void OnCameraTargetChanged(const neurus::CameraTargetChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Camera* cam = neurus::Camera::As(e.object);
	if (!cam) return;
	const glm::vec3 pos = cam->GetPosition();
	const glm::vec3 before = cam->cam_tar;
	const glm::vec3 after(e.targetX, e.targetY, e.targetZ);
	cam->SetTarPos(after);
	// Fold target edits into the coupled camera pose op (position unchanged) so
	// the panel and viewport navigation share one undoable "Camera Transform".
	ops.Submit(std::make_unique<neurus::CameraTransformOp>(
		cam->GetObjectID(),
		neurus::CameraPose{ pos, before },
		neurus::CameraPose{ pos, after }));
	Mutated(bus);
}

void OnCameraFovChanged(const neurus::CameraFovChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Camera* cam = neurus::Camera::As(e.object);
	if (!cam) return;
	const float before = cam->cam_pers;
	cam->ChangeCamPersp(e.fov);
	ops.Submit(std::make_unique<neurus::CameraFovOp>(cam->GetObjectID(), before, e.fov));
	Mutated(bus);
}

void OnCameraPoseChanged(const neurus::CameraPoseChanged& e, neurus::EventQueue& bus)
{
	neurus::Camera* cam = neurus::Camera::As(e.object);
	if (!cam) return;
	// Replay path for CameraTransformOp: apply the absolute pose. Non-recording;
	// live navigation records the op, this handler only re-applies endpoints.
	cam->SetPosition(glm::vec3(e.posX, e.posY, e.posZ));
	cam->SetTarPos(glm::vec3(e.tarX, e.tarY, e.tarZ));
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Mesh properties
// ---------------------------------------------------------------------------

void OnMeshShadowChanged(const neurus::MeshShadowChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Mesh* mesh = neurus::Mesh::As(e.object);
	if (!mesh) return;
	const bool before = mesh->using_shadow;
	mesh->EnableShadow(e.enabled);
	ops.Submit(std::make_unique<neurus::SetMeshShadowOp>(mesh->GetObjectID(), before, e.enabled));
	Mutated(bus);
}

void OnMeshMaterialChanged(const neurus::MeshMaterialChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Mesh* mesh = neurus::Mesh::As(e.object);
	if (!mesh) return;
	const bool before = mesh->using_material;
	mesh->EnableMaterial(e.enabled);
	ops.Submit(std::make_unique<neurus::SetMeshMaterialOp>(mesh->GetObjectID(), before, e.enabled));
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Light properties
// ---------------------------------------------------------------------------

void OnLightPowerChanged(const neurus::LightPowerChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	const float before = light->light_power;
	light->SetPower(e.power);
	ops.Submit(std::make_unique<neurus::SetLightPowerOp>(light->GetObjectID(), before, e.power));
	LightStructChanged(e.object, bus);
}

void OnLightColorChanged(const neurus::LightColorChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	const glm::vec3 before = light->light_color;
	const glm::vec3 after(e.r, e.g, e.b);
	light->SetColor(after);
	ops.Submit(std::make_unique<neurus::SetLightColorOp>(light->GetObjectID(), before, after));
	LightStructChanged(e.object, bus);
}

void OnLightRadiusChanged(const neurus::LightRadiusChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	const float before = light->light_radius;
	light->SetRadius(e.radius);
	ops.Submit(std::make_unique<neurus::SetLightRadiusOp>(light->GetObjectID(), before, e.radius));
	LightStructChanged(e.object, bus);
}

void OnLightShadowChanged(const neurus::LightShadowChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	const bool before = light->use_shadow;
	light->SetShadow(e.enabled);
	ops.Submit(std::make_unique<neurus::SetLightShadowOp>(light->GetObjectID(), before, e.enabled));
	LightingRebuilt(bus);
}

void OnLightCutoffChanged(const neurus::LightCutoffChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	const float before = light->spot_cutoff;
	light->SetCutoff(e.cutoff);
	ops.Submit(std::make_unique<neurus::SetLightCutoffOp>(light->GetObjectID(), before, e.cutoff));
	LightStructChanged(e.object, bus);
}

void OnLightOuterCutoffChanged(const neurus::LightOuterCutoffChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	const float before = light->spot_outer_cutoff;
	light->SetOuterCutoff(e.outerCutoff);
	ops.Submit(std::make_unique<neurus::SetLightOuterCutoffOp>(light->GetObjectID(), before, e.outerCutoff));
	LightStructChanged(e.object, bus);
}

// ---------------------------------------------------------------------------
// Environment properties
// ---------------------------------------------------------------------------

void OnEnvironmentIntensityChanged(const neurus::EnvironmentIntensityChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Environment* env = neurus::Environment::As(e.object);
	if (!env) return;
	const float before = env->GetIntensity();
	env->SetIntensity(e.intensity);
	ops.Submit(std::make_unique<neurus::SetEnvIntensityOp>(env->GetObjectID(), before, e.intensity));
	Mutated(bus);
}

void OnEnvironmentRotationChanged(const neurus::EnvironmentRotationChanged& e, neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Environment* env = neurus::Environment::As(e.object);
	if (!env) return;
	const float before = env->GetRotation();
	env->SetRotation(e.rotation);
	ops.Submit(std::make_unique<neurus::SetEnvRotationOp>(env->GetObjectID(), before, e.rotation));
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Scene membership (Add / Delete)
// ---------------------------------------------------------------------------

/** @brief Registers a pooled object into the scene by type (camera/mesh/light/env). */
void UsePooledObject(neurus::Scene& scene, neurus::ResourceManager& resources, int uid)
{
	auto obj = resources.Get<neurus::ObjectID>(uid);
	if (!obj) return;
	switch (obj->o_type)
	{
	case neurus::ObjectID::GOType::GO_CAM:
		scene.UseCamera(resources.Get<neurus::Camera>(uid));
		break;
	case neurus::ObjectID::GOType::GO_MESH:
		scene.UseMesh(resources.Get<neurus::Mesh>(uid));
		break;
	case neurus::ObjectID::GOType::GO_LIGHT:
	case neurus::ObjectID::GOType::GO_POLYLIGHT:
		scene.UseLight(resources.Get<neurus::Light>(uid));
		break;
	case neurus::ObjectID::GOType::GO_ENVIR:
		scene.UseEnvironment(resources.Get<neurus::Environment>(uid));
		break;
	default:
		break; // sprites / debug primitives: no add flow
	}
}

/** @brief Drops one object's scene reference by UID (typed erase; stale = no-op). */
bool RemoveSceneObject(neurus::Scene& scene, int uid)
{
	return scene.RemoveCamera(uid)
	    || scene.RemoveMesh(uid)
	    || scene.RemoveLight(uid)
	    || scene.RemoveEnvironment(uid);
}

/** @brief True if the object is a light (SSBO is a scene projection). */
bool IsLightObject(const neurus::ObjectID* obj)
{
	return obj && (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT
	               || obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT);
}

/** @brief Adds a pooled object to the scene, selects it, and records ONE composite op. */
void OnSceneObjectAddRequested(const neurus::SceneObjectAddRequested& e,
                               neurus::EventQueue& bus, neurus::IOperationSink& ops,
                               neurus::ResourceManager& resources)
{
	neurus::Scene* scene = neurus::Scene::As(e.scene);
	if (!scene) return;
	if (scene->GetObjectID(e.objectUid)) return; // already registered

	auto obj = resources.Get<neurus::ObjectID>(e.objectUid);
	if (!obj) return; // stale UID: resource no longer pooled

	const neurus::SelectionState before = SnapshotSelection(*scene);
	UsePooledObject(*scene, resources, e.objectUid);

	// Select the added object (set, non-incremental).
	scene->selections.Select(obj.get(), false);

	// Light SSBO mirrors scene->light_list: rebuild after the light is in.
	if (IsLightObject(obj.get()))
		LightingRebuilt(bus);
	else
		Mutated(bus);

	// One undo entry: add + select (composed selection op restores on undo).
	std::vector<std::unique_ptr<neurus::Operation>> seq;
	seq.push_back(std::make_unique<neurus::SceneObjectAddOp>(e.objectUid, true));
	const neurus::SelectionState after = SnapshotSelection(*scene);
	seq.push_back(std::make_unique<neurus::SetSelectionOp>(before, after));
	ops.Submit(std::make_unique<neurus::CompositeOp>(std::move(seq)));
}

/** @brief UI gesture: deselect all, delete every selected object, record ONE composite op. */
void OnObjectDeleteRequested(const neurus::ObjectDeleteRequested& e,
                             neurus::EventQueue& bus, neurus::IOperationSink& ops)
{
	neurus::Scene* scene = neurus::Scene::As(e.scene);
	if (!scene) return;

	const neurus::SelectionState before = SnapshotSelection(*scene);
	if (before.selectedUids.empty()) return;

	// Last-camera guard: the render passes dereference GetActiveCamera()
	// unconditionally, so never leave the scene without a camera. Types are
	// resolved from the SCENE (the objects being deleted are in it by
	// definition) — no pool dependency.
	size_t camerasToDelete = 0;
	for (int uid : before.selectedUids)
	{
		const neurus::ObjectID* obj = scene->GetObjectID(uid);
		if (obj && obj->o_type == neurus::ObjectID::GOType::GO_CAM)
			++camerasToDelete;
	}
	if (camerasToDelete > 0 && scene->cam_list.size() <= camerasToDelete)
	{
		NEURUS_ERR("[SceneController] Refusing to delete the last camera");
		return;
	}

	// Deselect all, then drop each selected object's scene reference.
	scene->selections.ClearSelection();

	std::vector<std::unique_ptr<neurus::Operation>> seq;
	seq.push_back(std::make_unique<neurus::SetSelectionOp>(before, neurus::SelectionState{}));
	bool removedLight = false;
	for (int uid : before.selectedUids)
	{
		removedLight |= IsLightObject(scene->GetObjectID(uid));
		if (RemoveSceneObject(*scene, uid))
			seq.push_back(std::make_unique<neurus::SceneObjectAddOp>(uid, false));
	}
	if (removedLight)
		bus.enqueue(neurus::LightingRebuild{});
	Mutated(bus);

	ops.Submit(std::make_unique<neurus::CompositeOp>(std::move(seq)));
}

/** @brief Replay-only: removes ONE object's scene reference (SceneObjectAddOp delete). */
void OnSceneObjectDeleteRequested(const neurus::SceneObjectDeleteRequested& e,
                                  neurus::EventQueue& bus)
{
	neurus::Scene* scene = neurus::Scene::As(e.scene);
	if (!scene) return;
	const bool wasLight = IsLightObject(scene->GetObjectID(e.objectUid));
	if (!RemoveSceneObject(*scene, e.objectUid)) return; // stale — safe no-op

	if (wasLight)
		LightingRebuilt(bus);
	else
		Mutated(bus);
}

} // anonymous namespace

namespace neurus {

void SceneController::Init(EventQueue& bus, IOperationSink& ops)
{
	bus.subscribe<ObjectSelected>([&bus, &ops](const ObjectSelected& e) { OnObjectSelected(e, bus, ops); });
	bus.subscribe<ObjectDeselected>([&bus, &ops](const ObjectDeselected& e) { OnObjectDeselected(e, bus, ops); });
	bus.subscribe<SelectionChanged>([&bus](const SelectionChanged& e) { OnSelectionChanged(e, bus); });
	bus.subscribe<VisibilityChanged>([&bus, &ops](const VisibilityChanged& e) { OnVisibilityChanged(e, bus, ops); });
	bus.subscribe<PositionChanged>([&bus, &ops](const PositionChanged& e) { OnPositionChanged(e, bus, ops); });
	bus.subscribe<RotationChanged>([&bus, &ops](const RotationChanged& e) { OnRotationChanged(e, bus, ops); });
	bus.subscribe<ScaleChanged>([&bus, &ops](const ScaleChanged& e) { OnScaleChanged(e, bus, ops); });
	bus.subscribe<CameraTargetChanged>([&bus, &ops](const CameraTargetChanged& e) { OnCameraTargetChanged(e, bus, ops); });
	bus.subscribe<CameraFovChanged>([&bus, &ops](const CameraFovChanged& e) { OnCameraFovChanged(e, bus, ops); });
	bus.subscribe<CameraPoseChanged>([&bus](const CameraPoseChanged& e) { OnCameraPoseChanged(e, bus); });
	bus.subscribe<MeshShadowChanged>([&bus, &ops](const MeshShadowChanged& e) { OnMeshShadowChanged(e, bus, ops); });
	bus.subscribe<MeshMaterialChanged>([&bus, &ops](const MeshMaterialChanged& e) { OnMeshMaterialChanged(e, bus, ops); });
	bus.subscribe<LightPowerChanged>([&bus, &ops](const LightPowerChanged& e) { OnLightPowerChanged(e, bus, ops); });
	bus.subscribe<LightColorChanged>([&bus, &ops](const LightColorChanged& e) { OnLightColorChanged(e, bus, ops); });
	bus.subscribe<LightRadiusChanged>([&bus, &ops](const LightRadiusChanged& e) { OnLightRadiusChanged(e, bus, ops); });
	bus.subscribe<LightShadowChanged>([&bus, &ops](const LightShadowChanged& e) { OnLightShadowChanged(e, bus, ops); });
	bus.subscribe<LightCutoffChanged>([&bus, &ops](const LightCutoffChanged& e) { OnLightCutoffChanged(e, bus, ops); });
	bus.subscribe<LightOuterCutoffChanged>([&bus, &ops](const LightOuterCutoffChanged& e) { OnLightOuterCutoffChanged(e, bus, ops); });
	bus.subscribe<EnvironmentIntensityChanged>([&bus, &ops](const EnvironmentIntensityChanged& e) { OnEnvironmentIntensityChanged(e, bus, ops); });
	bus.subscribe<EnvironmentRotationChanged>([&bus, &ops](const EnvironmentRotationChanged& e) { OnEnvironmentRotationChanged(e, bus, ops); });

	// --- Scene membership (add / delete) ---
	bus.subscribe<SceneObjectAddRequested>([&bus, &ops, this](const SceneObjectAddRequested& e) {
		OnSceneObjectAddRequested(e, bus, ops, *m_poolProvider());
	});
	bus.subscribe<SceneObjectDeleteRequested>([&bus, this](const SceneObjectDeleteRequested& e) {
		OnSceneObjectDeleteRequested(e, bus);
	});
	bus.subscribe<ObjectDeleteRequested>([&bus, &ops, this](const ObjectDeleteRequested& e) {
		OnObjectDeleteRequested(e, bus, ops);
	});
}

} // namespace neurus
