/**
 * @file SceneController.h
 * @brief Scene mutation controller -- event-driven, no per-frame polling.
 *
 * SceneController translates scene-domain events (selection, transform,
 * visibility, camera/mesh/light/environment property edits) into scene object
 * mutations. Fully stateless: all handlers are free functions in the .cpp.
 *
 * Events carry const ObjectID* (the object to mutate, cast to the concrete
 * type in the .cpp) and, for scene-owned state (selection), const UID* (the
 * Editor-owned Scene). GPU uploads stay in Editor: this controller emits
 * EditorEvents (SceneModified, LightGpuChanged, LightingRebuild) that Editor
 * subscribes to and executes against its DeferredRenderer/UploadManager.
 *
 * Event Mapping:
 *   - ObjectSelected / ObjectDeselected -> scene.selections.Select/Deselect
 *   - VisibilityChanged -> ObjectID::SetVisible (light -> LightingRebuild)
 *   - Position/Rotation/ScaleChanged -> Transform3D setters (light -> LightingRebuild)
 *   - CameraTarget/FovChanged -> Camera setters
 *   - MeshShadow/MaterialChanged -> Mesh::EnableShadow/EnableMaterial
 *   - LightPower/Radius/Cutoff/OuterCutoffChanged -> setters + LightGpuChanged
 *   - LightShadowChanged -> Light::SetShadow + LightingRebuild
 *   - EnvironmentIntensity/RotationChanged -> Environment setters
 */

#pragma once

#include "editor/controllers/Controllers.h"
#include "editor/events/EventBus.h"

namespace neurus {

class SceneController : public Controllers
{
public:
	SceneController() = default;

	/**
	 * @brief Subscribes to scene events on the given EventQueue.
	 *
	 * Registers lambda handlers that forward each event to the corresponding
	 * free-function handler. Must be called once during initialization,
	 * before any events are enqueued.
	 *
	 * @param bus EventQueue to subscribe to.
	 */
	void Init(EventQueue& bus) override;
};

} // namespace neurus
