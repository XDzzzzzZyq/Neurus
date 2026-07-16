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
 * @brief Emitted when the position of a selected object is edited from the Property Panel.
 *
 * Rotation values are in degrees (Euler: pitch=X, yaw=Z, roll=Y).
 */
struct PositionChanged
{
	int   objectId;      ///< Target scene object identifier.
	float posX = 0.0f;   ///< World-space X position.
	float posY = 0.0f;   ///< World-space Y position (forward).
	float posZ = 0.0f;   ///< World-space Z position (up).
};

/** @brief Emitted when the rotation of a selected object is edited. */
struct RotationChanged
{
	int   objectId;      ///< Target scene object identifier.
	float rotX = 0.0f;   ///< Pitch rotation in degrees.
	float rotY = 0.0f;   ///< Roll rotation in degrees.
	float rotZ = 0.0f;   ///< Yaw rotation in degrees.
};

/** @brief Emitted when the scale of a selected object is edited. */
struct ScaleChanged
{
	int   objectId;      ///< Target scene object identifier.
	float sclX = 1.0f;   ///< X-axis scale.
	float sclY = 1.0f;   ///< Y-axis scale.
	float sclZ = 1.0f;   ///< Z-axis scale.
};

// ---------------------------------------------------------------------------
// Camera property events
// ---------------------------------------------------------------------------

/** @brief Emitted when the camera look-at target is edited. */
struct CameraTargetChanged
{
	int   objectId;
	float targetX = 0.0f;
	float targetY = 0.0f;
	float targetZ = 0.0f;
};

/** @brief Emitted when the camera FOV is edited. */
struct CameraFovChanged
{
	int   objectId;
	float fov = 60.0f;
};

// ---------------------------------------------------------------------------
// Mesh property events
// ---------------------------------------------------------------------------

/** @brief Emitted when mesh shadow casting flag is toggled. */
struct MeshShadowChanged
{
	int  objectId;
	bool enabled = true;
};

/** @brief Emitted when mesh material usage flag is toggled. */
struct MeshMaterialChanged
{
	int  objectId;
	bool enabled = true;
};

// ---------------------------------------------------------------------------
// Light property events
// ---------------------------------------------------------------------------

/** @brief Emitted when light power/intensity is edited. */
struct LightPowerChanged
{
	int   objectId;
	float power = 10.0f;
};

/** @brief Emitted when light radius is edited. */
struct LightRadiusChanged
{
	int   objectId;
	float radius = 0.05f;
};

/** @brief Emitted when light shadow casting flag is toggled. */
struct LightShadowChanged
{
	int  objectId;
	bool enabled = true;
};

// ---------------------------------------------------------------------------
// Environment property events
// ---------------------------------------------------------------------------

/** @brief Emitted when IBL environment intensity is edited. */
struct EnvironmentIntensityChanged
{
	int   objectId;
	float intensity = 1.0f;
};

/** @brief Emitted when IBL environment rotation is edited. */
struct EnvironmentRotationChanged
{
	int   objectId;
	float rotation = 0.0f;
};

} // namespace neurus
