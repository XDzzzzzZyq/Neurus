/**
 * @file SceneEvents.h
 * @brief Ephemeral scene-domain events (UI -> Editor -> SceneController).
 *
 * Events are ephemeral: they are enqueued and processed within a single frame
 * and destroyed after execution. They carry object/scene POINTERS instead of
 * IDs so controllers never re-fetch objects from the Scene by ID:
 * - const ObjectID* object — the scene object being mutated (cast to the
 *   concrete type in the controller .cpp via the class static As() helper).
 * - const UID* scene       — the Editor-owned Scene, only for events whose
 *   handler touches scene-owned state (selection).
 *
 * The UI layer emits COMPLETE events (panels hold the scene pointer as UI
 * state, set during Refresh()); Editor::OnUIEvent just enqueues them unchanged.
 */

#pragma once

#include <string>
#include <vector>

namespace neurus {

class ObjectID;
class UID;

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

/** @brief Emitted when a scene object is selected (outliner click / viewport pick). */
struct ObjectSelected
{
	const UID* scene = nullptr;       ///< Editor-owned Scene (selection state).
	const ObjectID* object = nullptr; ///< Selected object (nullptr = background click).
	int modifiers = 0;                ///< Input::Modifiers bitmask.
};

/** @brief Emitted when a scene object is deselected. */
struct ObjectDeselected
{
	const UID* scene = nullptr;
	const ObjectID* object = nullptr;
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
	const ObjectID* object = nullptr;     ///< Object handle.
	bool viewportVisible = true;          ///< Viewport (editor) visibility.
	bool renderVisible = true;            ///< Render (pipeline) visibility.
};

// ---------------------------------------------------------------------------
// Transform (Property Panel)
// ---------------------------------------------------------------------------

/** @brief Emitted when the position of an object is edited. */
struct PositionChanged
{
	const ObjectID* object = nullptr;
	float posX = 0.0f;
	float posY = 0.0f;
	float posZ = 0.0f;
};

/** @brief Emitted when the rotation of an object is edited (degrees, Z-up Euler). */
struct RotationChanged
{
	const ObjectID* object = nullptr;
	float rotX = 0.0f;
	float rotY = 0.0f;
	float rotZ = 0.0f;
};

/** @brief Emitted when the scale of an object is edited. */
struct ScaleChanged
{
	const ObjectID* object = nullptr;
	float sclX = 1.0f;
	float sclY = 1.0f;
	float sclZ = 1.0f;
};

// ---------------------------------------------------------------------------
// Camera property events
// ---------------------------------------------------------------------------

struct CameraTargetChanged
{
	const ObjectID* object = nullptr;
	float targetX = 0.0f;
	float targetY = 0.0f;
	float targetZ = 0.0f;
};

struct CameraFovChanged
{
	const ObjectID* object = nullptr;
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
	const ObjectID* object = nullptr;
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
	const ObjectID* object = nullptr;
	bool enabled = true;
};

struct MeshMaterialChanged
{
	const ObjectID* object = nullptr;
	bool enabled = true;
};

// ---------------------------------------------------------------------------
// Light property events
// ---------------------------------------------------------------------------

struct LightPowerChanged
{
	const ObjectID* object = nullptr;
	float power = 10.0f;
};

struct LightColorChanged
{
	const ObjectID* object = nullptr;
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
};

struct LightRadiusChanged
{
	const ObjectID* object = nullptr;
	float radius = 0.05f;
};

struct LightShadowChanged
{
	const ObjectID* object = nullptr;
	bool enabled = true;
};

struct LightCutoffChanged
{
	const ObjectID* object = nullptr;
	float cutoff = 0.9f;
};

struct LightOuterCutoffChanged
{
	const ObjectID* object = nullptr;
	float outerCutoff = 0.8f;
};

// ---------------------------------------------------------------------------
// Environment property events
// ---------------------------------------------------------------------------

struct EnvironmentIntensityChanged
{
	const ObjectID* object = nullptr;
	float intensity = 1.0f;
};

struct EnvironmentRotationChanged
{
	const ObjectID* object = nullptr;
	float rotation = 0.0f;
};

// ---------------------------------------------------------------------------
// Scene membership (Add / Delete)
// ---------------------------------------------------------------------------

/**
 * @brief Adds an object to the scene.
 *
 * Emitted by the Editor after loading a resource into the pool (mesh import,
 * camera/light add) — FORWARD path only, never replayed. Carries the UID; the
 * SceneController fetches the pooled object from the ResourceManager by UID,
 * registers it, selects it, and records the composite operation. Replay uses
 * SceneObjectAddRestored (no gesture semantics).
 */
struct SceneObjectAddRequested
{
	const UID* scene = nullptr;  ///< Editor-owned Scene.
	int objectUid = 0;           ///< Pooled object UID.
};

/**
 * @brief Re-registers an object in the scene (pure restore).
 *
 * Emitted only by SceneObjectAddOp (add direction) on undo/redo replay.
 * The handler registers the pooled object WITHOUT touching selection and
 * WITHOUT recording — selection is restored by the composite's own
 * SetSelectionOp. Mirrors the ShaderCodeRestored convention: forward
 * gesture events and replay-restore events are distinct types.
 */
struct SceneObjectAddRestored
{
	const UID* scene = nullptr;
	int objectUid = 0;
};

/**
 * @brief Removes ONE object's scene reference.
 *
 * Emitted only by SceneObjectAddOp (delete direction) on undo/redo replay;
 * the handler removes exactly this UID — no selection logic, no operation
 * recording. The pooled resource is never removed from the pool.
 */
struct SceneObjectDeleteRequested
{
	const UID* scene = nullptr;
	int objectUid = 0;
};

/**
 * @brief UI gesture: delete ALL selected objects.
 *
 * Emitted by the Outliner / Viewport when the Delete key is pressed. The
 * SceneController snapshots the selection, guards the last camera, deselects,
 * removes every selected object, and records ONE composite operation
 * (selection-clear + per-object delete).
 */
struct ObjectDeleteRequested
{
	const UID* scene = nullptr;
};

} // namespace neurus
