/**
 * @file SceneEvents.h
 * @brief Ephemeral scene-domain events (UI -> Editor -> SceneController).
 *
 * Events are ephemeral: they are enqueued and processed within a single frame
 * and destroyed after execution. They carry object/scene POINTERS instead of
 * IDs so controllers never re-fetch objects from the Scene by ID:
 * - const ObjectID* object — the scene object being mutated (cast to the
 *   concrete type in the controller .cpp, like ShaderController::AsMesh).
 * - const UID* scene       — the Editor-owned Scene, only for events whose
 *   handler touches scene-owned state (selection).
 *
 * The UI layer emits COMPLETE events (panels hold the scene pointer as UI
 * state, set during Refresh()); Editor::OnUIEvent just enqueues them unchanged.
 */

#pragma once

#include <string>

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

} // namespace neurus
