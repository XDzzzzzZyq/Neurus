/**
 * @file OperationContext.h
 * @brief Per-replay context passed to Operation::Apply().
 *
 * Operations are pure value descriptors: they replay by dispatching their
 * absolute-set event SYNCHRONOUSLY on the event queue, carrying integer object
 * UIDs. They never resolve objects themselves — the controller handler that
 * receives the replayed event resolves the UID against the current scene/pool
 * via its ControllerContext (stale UIDs no-op there). The OperationManager
 * therefore needs no scene or resource access at replay time.
 *
 * Constructed fresh by OperationManager on each Undo/Redo from its EventQueue.
 */

#pragma once

#include "editor/events/IEventQueue.h"

namespace neurus {

/**
 * @brief Context for replaying an operation (event dispatch only).
 */
struct OperationContext
{
	/// @brief Queue the operation replays its event on (via emitNow).
	IEventQueue& events;
};

} // namespace neurus
