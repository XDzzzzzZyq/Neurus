/**
 * @file SceneEvents.h
 * @brief Ephemeral scene-domain events (UI -> Editor -> SceneController).
 *
 * Events are ephemeral: they are enqueued and processed within a single frame
 * and destroyed after execution. They carry object/scene POINTERS instead of
 * IDs so controllers never re-fetch objects from the Scene by ID:
 * - const UID* object - the scene object being mutated, upcast from the
 *   concrete ObjectID-derived pointer; cast back in the controller .cpp via
 *   the untyped ObjectID::As or the templated ObjectID::As<T> (which compares
 *   o_type against T::Type).
 * - const UID* scene  - the Editor-owned Scene, upcast from Scene*; cast back
 *   via Scene::As. Only for events whose handler touches scene-owned state
 *   (selection).
 *
 * The UI layer emits PURE INPUT INTENTS on the UI->Editor path (panels no
 * longer hold or stamp the scene); scene editing events carry const UID* and
 * Editor::OnUIEvent enqueues them unchanged.
 */

#pragma once

#include <string>
#include <vector>

namespace neurus {

class UID;

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

/** @brief Emitted when a scene object is selected (outliner click / viewport pick). */
struct ObjectSelected
{
	const UID* scene = nullptr;       ///< Editor-owned Scene (selection state).
	const UID* object = nullptr; ///< Selected object (nullptr = background click).
	int modifiers = 0;                ///< Input::Modifiers bitmask.
};

/** @brief Emitted when a scene object is deselected. */
struct ObjectDeselected
{
	const UID* scene = nullptr;
	const UID* object = nullptr;
};

/**
 * @brief Absolute selection-set state applied to the scene.
 *
 * Emitted only when replaying a SetSelectionOp: live selection events
 * (ObjectSelected/ObjectDeselected) mutate the set incrementally, so the
 * undoable operation stores the absolute selected-UID list plus the active
 * UID and dispatches this to restore the whole set at once.
 */
struct SelectionChanged
{
	const UID* scene = nullptr;       ///< Editor-owned Scene (selection state).
	std::vector<int> selectedUids;    ///< Ordered selected object UIDs.
	int activeUid = 0;                ///< Active object UID (0 = none).
};

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------

/** @brief Emitted when object visibility toggles change in the outliner. */
struct VisibilityChanged
{
	const UID* object = nullptr;     ///< Object handle.
	bool viewportVisible = true;          ///< Viewport (editor) visibility.
	bool renderVisible = true;            ///< Render (pipeline) visibility.
};

// ---------------------------------------------------------------------------
// Transform (Property Panel)
// ---------------------------------------------------------------------------

/** @brief Emitted when the position of an object is edited. */
struct PositionChanged
{
	const UID* object = nullptr;
	float posX = 0.0f;
	float posY = 0.0f;
	float posZ = 0.0f;
};

/** @brief Emitted when the rotation of an object is edited (degrees, Z-up Euler). */
struct RotationChanged
{
	const UID* object = nullptr;
	float rotX = 0.0f;
	float rotY = 0.0f;
	float rotZ = 0.0f;
};

/** @brief Emitted when the scale of an object is edited. */
struct ScaleChanged
{
	const UID* object = nullptr;
	float sclX = 1.0f;
	float sclY = 1.0f;
	float sclZ = 1.0f;
};

// ---------------------------------------------------------------------------
// Camera property events
// ---------------------------------------------------------------------------

struct CameraTargetChanged
{
	const UID* object = nullptr;
	float targetX = 0.0f;
	float targetY = 0.0f;
	float targetZ = 0.0f;
};

struct CameraFovChanged
{
	const UID* object = nullptr;
	float fov = 60.0f;
};

/**
 * @brief Absolute camera pose (position + look-at target) set.
 *
 * Emitted only when replaying a CameraTransformOp: live navigation events
 * (rotate/slide/push/zoom) carry relative deltas and cannot be replayed, so
 * the undoable operation stores the absolute endpoints and dispatches this.
 */
struct CameraPoseChanged
{
	const UID* object = nullptr;
	float posX = 0.0f;
	float posY = 0.0f;
	float posZ = 0.0f;
	float tarX = 0.0f;
	float tarY = 0.0f;
	float tarZ = 0.0f;
};

// ---------------------------------------------------------------------------
// Mesh property events
// ---------------------------------------------------------------------------

struct MeshShadowChanged
{
	const UID* object = nullptr;
	bool enabled = true;
};

struct MeshMaterialChanged
{
	const UID* object = nullptr;
	bool enabled = true;
};

// ---------------------------------------------------------------------------
// Light property events
// ---------------------------------------------------------------------------

struct LightPowerChanged
{
	const UID* object = nullptr;
	float power = 10.0f;
};

struct LightColorChanged
{
	const UID* object = nullptr;
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
};

struct LightRadiusChanged
{
	const UID* object = nullptr;
	float radius = 0.05f;
};

struct LightShadowChanged
{
	const UID* object = nullptr;
	bool enabled = true;
};

struct LightCutoffChanged
{
	const UID* object = nullptr;
	float cutoff = 0.9f;
};

struct LightOuterCutoffChanged
{
	const UID* object = nullptr;
	float outerCutoff = 0.8f;
};

// ---------------------------------------------------------------------------
// Environment property events
// ---------------------------------------------------------------------------

struct EnvironmentIntensityChanged
{
	const UID* object = nullptr;
	float intensity = 1.0f;
};

struct EnvironmentRotationChanged
{
	const UID* object = nullptr;
	float rotation = 0.0f;
};

// ---------------------------------------------------------------------------
// Scene membership (Add / Delete)
// ---------------------------------------------------------------------------

/**
 * @brief Adds an object to the scene.
 *
 * Emitted by the Editor after loading a resource into the pool (mesh import,
 * camera/light add) AND by SceneObjectAddOp on undo/redo replay (the
 * originating event — replay runs the same handler as a live edit; the
 * handler's Submit is muted by Phase::Replaying). Carries the UID; the
 * SceneController fetches the pooled object from the ResourceManager by UID,
 * registers it, selects it, and records the composite operation.
 */
struct SceneObjectAddRequested
{
	const UID* scene = nullptr;  ///< Editor-owned Scene.
	int objectUid = 0;           ///< Pooled object UID.
};

/**
 * @brief Removes a BATCH of objects' scene references.
 *
 * The SINGLE removal path: emitted by the delete gesture (one event carrying
 * all selected UIDs) AND by SceneObjectAddOp (delete direction) on undo/redo
 * replay. The handler removes exactly these UIDs — no selection logic, no
 * operation recording (the gesture records the composite; replay is muted).
 * The pooled resources are never removed from the pool.
 */
struct SceneObjectDeleteRequested
{
	const UID* scene = nullptr;    ///< Editor-owned Scene.
	std::vector<int> uids;         ///< Object UIDs to remove (one or more).
};

/**
 * @brief Editor->Controller delete gesture: delete ALL selected objects (Delete key).
 *
 * Dedicated Editor->Controller event: the Editor emits it (wrapping the pure
 * DeleteRequested input intent) with scene = m_scene.get(); it is no longer
 * emitted by any UI panel and no panel stamps the scene.
 * FORWARD-ONLY: the recorded composite replays via SceneObjectDeleteRequested,
 * never this gesture event. The SceneController snapshots the selection,
 * guards the last camera, deselects, removes every selected object (one
 * batched SceneObjectDeleteRequested), and records ONE composite operation
 * (selection-clear + batched delete).
 */
struct ObjectDeleteRequested
{
	const UID* scene = nullptr;  ///< Editor-owned Scene.
};

} // namespace neurus
