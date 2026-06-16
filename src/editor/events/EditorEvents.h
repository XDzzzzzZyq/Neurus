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

} // namespace neurus
