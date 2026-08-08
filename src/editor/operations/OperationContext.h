/**
 * @file OperationContext.h
 * @brief Per-replay context passed to Operation::Apply().
 *
 * Bundles the live Scene (for resolving an operation's stored UID back to the
 * current object) and the EventQueue used to replay the operation's event.
 * Constructed fresh by OperationManager on each Undo/Redo, so it always
 * references the current scene even after a scene swap (New/Load).
 *
 * Following EditorContext, `scene` is held as its `UID` base so this header does
 * not pull in the heavier scene/Scene.h; the UID -> object resolution downcast
 * lives in OperationContext.cpp.
 */

#pragma once

#include "editor/events/EventBus.h"
#include "scene/ObjectID.h"

namespace neurus {

/**
 * @brief Context for replaying an operation (scene + event queue).
 */
struct OperationContext
{
	UID& scene;      ///< Live scene (upcast to UID base), for UID -> object resolution.
	EventQueue& bus; ///< Queue the operation replays its event on (via emitNow).

	/**
	 * @brief Resolves a stored UID to the current scene object.
	 * @param uid Object UID captured when the operation was recorded.
	 * @return Non-owning object pointer, or nullptr if no longer present.
	 */
	ObjectID* Resolve(int uid) const;
};

} // namespace neurus
