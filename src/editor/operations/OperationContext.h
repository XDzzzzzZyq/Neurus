/**
 * @file OperationContext.h
 * @brief Per-replay context passed to Operation::Emit().
 *
 * Bundles the live Scene (for resolving an operation's stored UID back to the
 * current object) and the EventQueue used to replay the operation's event.
 * Constructed fresh by OperationManager on each Undo/Redo, so it always
 * references the current scene even after a scene swap (New/Load).
 */

#pragma once

#include "editor/events/EventBus.h"
#include "scene/Scene.h"
#include "scene/UID.h"

namespace neurus {

/**
 * @brief Context for replaying an operation (scene + event queue).
 */
struct OperationContext
{
	Scene& scene;    ///< Live scene, for UID -> object resolution.
	EventQueue& bus; ///< Queue the operation replays its event on (via emit).

	/**
	 * @brief Resolves a stored UID to the current scene object.
	 * @param uid Object UID captured when the operation was recorded.
	 * @return Non-owning object pointer, or nullptr if no longer present.
	 */
	ObjectID* Resolve(int uid) const { return scene.GetObjectID(uid); }
};

} // namespace neurus
