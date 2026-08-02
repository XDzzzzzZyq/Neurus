/**
 * @file SceneController.cpp
 * @brief Event-driven scene mutation handlers (selection, transforms, props).
 *
 * Stateless -- all handlers are free functions in an anonymous namespace.
 * Each handler receives a discrete scene event, extracts the const ObjectID*
 * pointer, casts it to the concrete object type (Mesh*/Light*/Camera*...),
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

#include "editor/events/SceneEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/Input.h"

#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "scene/Transform.h"
#include "scene/UID.h"

#include "core/Log.h"

namespace {

// ---------------------------------------------------------------------------
// Cast helpers (const ObjectID* -> concrete type; like ShaderController::AsMesh)
// ---------------------------------------------------------------------------

neurus::Scene* AsScene(const neurus::UID* scene)
{
	if (!scene) return nullptr;
	return static_cast<neurus::Scene*>(const_cast<neurus::UID*>(scene));
}

neurus::Mesh* AsMesh(const neurus::ObjectID* obj)
{
	if (!obj || obj->o_type != neurus::ObjectID::GOType::GO_MESH)
		return nullptr;
	return static_cast<neurus::Mesh*>(const_cast<neurus::ObjectID*>(obj));
}

neurus::Light* AsLight(const neurus::ObjectID* obj)
{
	if (!obj) return nullptr;
	if (obj->o_type != neurus::ObjectID::GOType::GO_LIGHT &&
	    obj->o_type != neurus::ObjectID::GOType::GO_POLYLIGHT)
		return nullptr;
	return static_cast<neurus::Light*>(const_cast<neurus::ObjectID*>(obj));
}

neurus::Camera* AsCamera(const neurus::ObjectID* obj)
{
	if (!obj || obj->o_type != neurus::ObjectID::GOType::GO_CAM)
		return nullptr;
	return static_cast<neurus::Camera*>(const_cast<neurus::ObjectID*>(obj));
}

neurus::Environment* AsEnvironment(const neurus::ObjectID* obj)
{
	if (!obj || obj->o_type != neurus::ObjectID::GOType::GO_ENVIR)
		return nullptr;
	return static_cast<neurus::Environment*>(const_cast<neurus::ObjectID*>(obj));
}

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

void OnObjectSelected(const neurus::ObjectSelected& e, neurus::EventQueue&)
{
	neurus::Scene* scene = AsScene(e.scene);
	if (!scene) return;

	const bool increment = (e.modifiers & (neurus::Input::Mod_Shift | neurus::Input::Mod_Ctrl)) != 0;

	if (!e.object)
	{
		// Background click (objectId 0) -> clear selection
		if (!increment) scene->selections.ClearSelection();
		return;
	}

	scene->selections.Select(e.object, increment);
}

void OnObjectDeselected(const neurus::ObjectDeselected& e, neurus::EventQueue&)
{
	neurus::Scene* scene = AsScene(e.scene);
	if (!scene || !e.object) return;
	scene->selections.Deselect(e.object, false);
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------

void OnVisibilityChanged(const neurus::VisibilityChanged& e, neurus::EventQueue& bus)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;

	obj->SetVisible(e.viewportVisible, e.renderVisible);

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

void OnPositionChanged(const neurus::PositionChanged& e, neurus::EventQueue& bus)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;
	void* transformPtr = obj->GetTransform();
	if (!transformPtr) return;
	static_cast<neurus::Transform3D*>(transformPtr)->SetPosition(glm::vec3(e.posX, e.posY, e.posZ));

	if (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT ||
	    obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT)
		LightingRebuilt(bus);
	else
		Mutated(bus);
}

void OnRotationChanged(const neurus::RotationChanged& e, neurus::EventQueue& bus)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;
	void* transformPtr = obj->GetTransform();
	if (!transformPtr) return;
	static_cast<neurus::Transform3D*>(transformPtr)->SetRotation(glm::vec3(e.rotX, e.rotY, e.rotZ));

	if (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT ||
	    obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT)
		LightingRebuilt(bus);
	else
		Mutated(bus);
}

void OnScaleChanged(const neurus::ScaleChanged& e, neurus::EventQueue& bus)
{
	neurus::ObjectID* obj = const_cast<neurus::ObjectID*>(e.object);
	if (!obj) return;
	void* transformPtr = obj->GetTransform();
	if (!transformPtr) return;
	static_cast<neurus::Transform3D*>(transformPtr)->SetScale(glm::vec3(e.sclX, e.sclY, e.sclZ));

	if (obj->o_type == neurus::ObjectID::GOType::GO_LIGHT ||
	    obj->o_type == neurus::ObjectID::GOType::GO_POLYLIGHT)
		LightingRebuilt(bus);
	else
		Mutated(bus);
}

// ---------------------------------------------------------------------------
// Camera properties
// ---------------------------------------------------------------------------

void OnCameraTargetChanged(const neurus::CameraTargetChanged& e, neurus::EventQueue& bus)
{
	neurus::Camera* cam = AsCamera(e.object);
	if (!cam) return;
	cam->SetTarPos(glm::vec3(e.targetX, e.targetY, e.targetZ));
	Mutated(bus);
}

void OnCameraFovChanged(const neurus::CameraFovChanged& e, neurus::EventQueue& bus)
{
	neurus::Camera* cam = AsCamera(e.object);
	if (!cam) return;
	cam->ChangeCamPersp(e.fov);
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Mesh properties
// ---------------------------------------------------------------------------

void OnMeshShadowChanged(const neurus::MeshShadowChanged& e, neurus::EventQueue& bus)
{
	neurus::Mesh* mesh = AsMesh(e.object);
	if (!mesh) return;
	mesh->EnableShadow(e.enabled);
	Mutated(bus);
}

void OnMeshMaterialChanged(const neurus::MeshMaterialChanged& e, neurus::EventQueue& bus)
{
	neurus::Mesh* mesh = AsMesh(e.object);
	if (!mesh) return;
	mesh->EnableMaterial(e.enabled);
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Light properties
// ---------------------------------------------------------------------------

void OnLightPowerChanged(const neurus::LightPowerChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = AsLight(e.object);
	if (!light) return;
	light->SetPower(e.power);
	LightStructChanged(e.object, bus);
}

void OnLightRadiusChanged(const neurus::LightRadiusChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = AsLight(e.object);
	if (!light) return;
	light->SetRadius(e.radius);
	LightStructChanged(e.object, bus);
}

void OnLightShadowChanged(const neurus::LightShadowChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = AsLight(e.object);
	if (!light) return;
	light->SetShadow(e.enabled);
	LightingRebuilt(bus);
}

void OnLightCutoffChanged(const neurus::LightCutoffChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = AsLight(e.object);
	if (!light) return;
	light->SetCutoff(e.cutoff);
	LightStructChanged(e.object, bus);
}

void OnLightOuterCutoffChanged(const neurus::LightOuterCutoffChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = AsLight(e.object);
	if (!light) return;
	light->SetOuterCutoff(e.outerCutoff);
	LightStructChanged(e.object, bus);
}

// ---------------------------------------------------------------------------
// Environment properties
// ---------------------------------------------------------------------------

void OnEnvironmentIntensityChanged(const neurus::EnvironmentIntensityChanged& e, neurus::EventQueue& bus)
{
	neurus::Environment* env = AsEnvironment(e.object);
	if (!env) return;
	env->SetIntensity(e.intensity);
	Mutated(bus);
}

void OnEnvironmentRotationChanged(const neurus::EnvironmentRotationChanged& e, neurus::EventQueue& bus)
{
	neurus::Environment* env = AsEnvironment(e.object);
	if (!env) return;
	env->SetRotation(e.rotation);
	Mutated(bus);
}

} // anonymous namespace

namespace neurus {

void SceneController::Init(EventQueue& bus)
{
	bus.subscribe<ObjectSelected>([&bus](const ObjectSelected& e) { OnObjectSelected(e, bus); });
	bus.subscribe<ObjectDeselected>([&bus](const ObjectDeselected& e) { OnObjectDeselected(e, bus); });
	bus.subscribe<VisibilityChanged>([&bus](const VisibilityChanged& e) { OnVisibilityChanged(e, bus); });
	bus.subscribe<PositionChanged>([&bus](const PositionChanged& e) { OnPositionChanged(e, bus); });
	bus.subscribe<RotationChanged>([&bus](const RotationChanged& e) { OnRotationChanged(e, bus); });
	bus.subscribe<ScaleChanged>([&bus](const ScaleChanged& e) { OnScaleChanged(e, bus); });
	bus.subscribe<CameraTargetChanged>([&bus](const CameraTargetChanged& e) { OnCameraTargetChanged(e, bus); });
	bus.subscribe<CameraFovChanged>([&bus](const CameraFovChanged& e) { OnCameraFovChanged(e, bus); });
	bus.subscribe<MeshShadowChanged>([&bus](const MeshShadowChanged& e) { OnMeshShadowChanged(e, bus); });
	bus.subscribe<MeshMaterialChanged>([&bus](const MeshMaterialChanged& e) { OnMeshMaterialChanged(e, bus); });
	bus.subscribe<LightPowerChanged>([&bus](const LightPowerChanged& e) { OnLightPowerChanged(e, bus); });
	bus.subscribe<LightRadiusChanged>([&bus](const LightRadiusChanged& e) { OnLightRadiusChanged(e, bus); });
	bus.subscribe<LightShadowChanged>([&bus](const LightShadowChanged& e) { OnLightShadowChanged(e, bus); });
	bus.subscribe<LightCutoffChanged>([&bus](const LightCutoffChanged& e) { OnLightCutoffChanged(e, bus); });
	bus.subscribe<LightOuterCutoffChanged>([&bus](const LightOuterCutoffChanged& e) { OnLightOuterCutoffChanged(e, bus); });
	bus.subscribe<EnvironmentIntensityChanged>([&bus](const EnvironmentIntensityChanged& e) { OnEnvironmentIntensityChanged(e, bus); });
	bus.subscribe<EnvironmentRotationChanged>([&bus](const EnvironmentRotationChanged& e) { OnEnvironmentRotationChanged(e, bus); });
}

} // namespace neurus
