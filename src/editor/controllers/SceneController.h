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

#include <functional>

#include "editor/controllers/Controllers.h"
#include "editor/events/EventBus.h"

namespace neurus {

class ResourceManager;

class SceneController : public Controllers
{
public:
	/**
	 * @brief Constructs the controller with access to the resource pool.
	 * @param poolProvider Returns the Editor-owned ResourceManager (re-queried
	 *        per event, so a pool swap on project new/open stays safe).
	 * @note Constructed manually by the Editor (RegisterController<T>() cannot
	 *       supply the provider) — same pattern as RenderConfigController.
	 */
	explicit SceneController(std::function<ResourceManager*()> poolProvider)
		: m_poolProvider(std::move(poolProvider))
	{}

	/**
	 * @brief Subscribes to scene events on the given EventQueue.
	 *
	 * Registers lambda handlers that forward each event to the corresponding
	 * free-function handler. Property/transform handlers capture the before
	 * value, mutate, then record a forward operation via the sink. Must be
	 * called once during initialization, before any events are enqueued.
	 *
	 * @param bus EventQueue to subscribe to.
	 * @param ops Sink for recording undoable operations.
	 */
	void Init(EventQueue& bus, IOperationSink& ops) override;

private:
	/// Pool provider for UID -> object resolution (add/delete membership).
	std::function<ResourceManager*()> m_poolProvider;
};

} // namespace neurus
