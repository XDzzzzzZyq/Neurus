#pragma once

#include <string>

namespace neurus {

// ---------------------------------------------------------------------------
// Editor Events - domain events for Editor state changes
// ---------------------------------------------------------------------------

/** @brief Emitted when a scene object is selected by the user. */
struct ObjectSelected
{
	int objectId;
	int modifiers;  ///< Modifier key bitmask (Input::Modifiers flags).
};

/** @brief Emitted when a scene object is deselected. */
struct ObjectDeselected
{
	int objectId;
};

/** @brief Emitted when a new scene object is created. */
struct SceneObjectAdded
{
	int objectId;
	std::string typeName;
};

/** @brief Emitted when a scene object is removed/deleted. */
struct SceneObjectRemoved
{
	int objectId;
};

/** @brief Emitted when the active scene camera is switched.
 *  @note cameraId of -1 means no active camera. */
struct ActiveCameraChanged
{
	int cameraId;
};

/** @brief Emitted when scene modification status changes.
 *  @note status is a bitfield of SceneModifStatus flags. */
struct SceneStatusChanged
{
	int status;
};

/** @brief Emitted when an entity is selected in the outliner or viewport. */
struct EntitySelected
{
	int entityId;
};

/** @brief Emitted when a scene file is loaded. */
struct SceneLoaded
{
	std::string path;
};

/** @brief Emitted when material properties are changed. */
struct MaterialChanged
{
	int materialId;
};

/**
 * @brief Emitted when the active IBL environment is loaded or changed.
 *
 * The Renderer subscribes to this event to regenerate diffuse/specular
 * cubemaps via IBLPass::Generate(). The event carries the IDs needed
 * to locate the Environment object in the Scene's env_list pool.
 */
struct EnvironmentChanged
{
	int sceneId = -1; ///< ID of the Scene containing the Environment
	int envId   = -1; ///< ID of the Environment object (from UID::GetObjectID())
};

/** @brief Emitted when object visibility toggles change in the outliner. */
struct VisibilityChanged
{
	int  objectId;        ///< Unique object identifier.
	bool viewportVisible; ///< Viewport (editor) visibility.
	bool renderVisible;   ///< Render (pipeline) visibility.
};

/**
 * @brief Emitted when the transform of a selected object is edited from the Property Panel.
 *
 * Carries the full position/rotation/scale state after an edit. The Editor
 * layer applies these values to the scene object's Transform3D directly.
 * Rotation values are in degrees (Euler: pitch=X, yaw=Z, roll=Y).
 */
struct TransformChanged
{
	int   objectId;            ///< Target scene object identifier.
	float posX = 0.0f;         ///< World-space X position.
	float posY = 0.0f;         ///< World-space Y position (forward).
	float posZ = 0.0f;         ///< World-space Z position (up).
	float rotX = 0.0f;         ///< Pitch rotation in degrees.
	float rotY = 0.0f;         ///< Roll rotation in degrees.
	float rotZ = 0.0f;         ///< Yaw rotation in degrees.
	float sclX = 1.0f;         ///< X-axis scale.
	float sclY = 1.0f;         ///< Y-axis scale.
	float sclZ = 1.0f;         ///< Z-axis scale.
};

} // namespace neurus
