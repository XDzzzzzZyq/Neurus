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

#include "editor/events/SceneEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/operations/IOperationSink.h"
#include "editor/operations/SceneOperations.h"
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
	neurus::Scene* scene = neurus::Scene::As(e.scene);
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
	neurus::Scene* scene = neurus::Scene::As(e.scene);
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

void OnCameraTargetChanged(const neurus::CameraTargetChanged& e, neurus::EventQueue& bus)
{
	neurus::Camera* cam = neurus::Camera::As(e.object);
	if (!cam) return;
	cam->SetTarPos(glm::vec3(e.targetX, e.targetY, e.targetZ));
	Mutated(bus);
}

void OnCameraFovChanged(const neurus::CameraFovChanged& e, neurus::EventQueue& bus)
{
	neurus::Camera* cam = neurus::Camera::As(e.object);
	if (!cam) return;
	cam->ChangeCamPersp(e.fov);
	Mutated(bus);
}

// ---------------------------------------------------------------------------
// Mesh properties
// ---------------------------------------------------------------------------

void OnMeshShadowChanged(const neurus::MeshShadowChanged& e, neurus::EventQueue& bus)
{
	neurus::Mesh* mesh = neurus::Mesh::As(e.object);
	if (!mesh) return;
	mesh->EnableShadow(e.enabled);
	Mutated(bus);
}

void OnMeshMaterialChanged(const neurus::MeshMaterialChanged& e, neurus::EventQueue& bus)
{
	neurus::Mesh* mesh = neurus::Mesh::As(e.object);
	if (!mesh) return;
	mesh->EnableMaterial(e.enabled);
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

void OnLightRadiusChanged(const neurus::LightRadiusChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	light->SetRadius(e.radius);
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

void OnLightCutoffChanged(const neurus::LightCutoffChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	light->SetCutoff(e.cutoff);
	LightStructChanged(e.object, bus);
}

void OnLightOuterCutoffChanged(const neurus::LightOuterCutoffChanged& e, neurus::EventQueue& bus)
{
	neurus::Light* light = neurus::Light::As(e.object);
	if (!light) return;
	light->SetOuterCutoff(e.outerCutoff);
	LightStructChanged(e.object, bus);
}

// ---------------------------------------------------------------------------
// Environment properties
// ---------------------------------------------------------------------------

void OnEnvironmentIntensityChanged(const neurus::EnvironmentIntensityChanged& e, neurus::EventQueue& bus)
{
	neurus::Environment* env = neurus::Environment::As(e.object);
	if (!env) return;
	env->SetIntensity(e.intensity);
	Mutated(bus);
}

void OnEnvironmentRotationChanged(const neurus::EnvironmentRotationChanged& e, neurus::EventQueue& bus)
{
	neurus::Environment* env = neurus::Environment::As(e.object);
	if (!env) return;
	env->SetRotation(e.rotation);
	Mutated(bus);
}

} // anonymous namespace

namespace neurus {

void SceneController::Init(EventQueue& bus, IOperationSink& ops)
{
	bus.subscribe<ObjectSelected>([&bus](const ObjectSelected& e) { OnObjectSelected(e, bus); });
	bus.subscribe<ObjectDeselected>([&bus](const ObjectDeselected& e) { OnObjectDeselected(e, bus); });
	bus.subscribe<VisibilityChanged>([&bus](const VisibilityChanged& e) { OnVisibilityChanged(e, bus); });
	bus.subscribe<PositionChanged>([&bus, &ops](const PositionChanged& e) { OnPositionChanged(e, bus, ops); });
	bus.subscribe<RotationChanged>([&bus, &ops](const RotationChanged& e) { OnRotationChanged(e, bus, ops); });
	bus.subscribe<ScaleChanged>([&bus, &ops](const ScaleChanged& e) { OnScaleChanged(e, bus, ops); });
	bus.subscribe<CameraTargetChanged>([&bus](const CameraTargetChanged& e) { OnCameraTargetChanged(e, bus); });
	bus.subscribe<CameraFovChanged>([&bus](const CameraFovChanged& e) { OnCameraFovChanged(e, bus); });
	bus.subscribe<MeshShadowChanged>([&bus](const MeshShadowChanged& e) { OnMeshShadowChanged(e, bus); });
	bus.subscribe<MeshMaterialChanged>([&bus](const MeshMaterialChanged& e) { OnMeshMaterialChanged(e, bus); });
	bus.subscribe<LightPowerChanged>([&bus, &ops](const LightPowerChanged& e) { OnLightPowerChanged(e, bus, ops); });
	bus.subscribe<LightColorChanged>([&bus, &ops](const LightColorChanged& e) { OnLightColorChanged(e, bus, ops); });
	bus.subscribe<LightRadiusChanged>([&bus](const LightRadiusChanged& e) { OnLightRadiusChanged(e, bus); });
	bus.subscribe<LightShadowChanged>([&bus, &ops](const LightShadowChanged& e) { OnLightShadowChanged(e, bus, ops); });
	bus.subscribe<LightCutoffChanged>([&bus](const LightCutoffChanged& e) { OnLightCutoffChanged(e, bus); });
	bus.subscribe<LightOuterCutoffChanged>([&bus](const LightOuterCutoffChanged& e) { OnLightOuterCutoffChanged(e, bus); });
	bus.subscribe<EnvironmentIntensityChanged>([&bus](const EnvironmentIntensityChanged& e) { OnEnvironmentIntensityChanged(e, bus); });
	bus.subscribe<EnvironmentRotationChanged>([&bus](const EnvironmentRotationChanged& e) { OnEnvironmentRotationChanged(e, bus); });
}

} // namespace neurus
