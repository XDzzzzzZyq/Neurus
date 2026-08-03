/**
 * @file IOperationSink.h
 * @brief Interface controllers use to record undoable operations.
 *
 * Controllers depend only on this narrow interface, never on the concrete
 * OperationManager. At mutation time a handler captures the before-value,
 * applies the change, then Submit()s a forward operation describing the edit.
 *
 * Submit() is a no-op while the manager is replaying (Undo/Redo): replay
 * re-runs the same handlers synchronously, and re-recording during replay
 * would corrupt the history stacks. The suppression lives in the sink so
 * handlers stay ignorant of replay state.
 */

#pragma once

#include <memory>

namespace neurus {

class Operation;

/**
 * @brief Sink that records forward operations for undo/redo.
 */
class IOperationSink
{
public:
	virtual ~IOperationSink() = default;

	/**
	 * @brief Records a forward operation describing a just-applied edit.
	 * @param op Operation with absolute before/after values (ownership taken).
	 * @note No-op during replay (Undo/Redo). Recording a new forward
	 *       operation clears the redo stack.
	 */
	virtual void Submit(std::unique_ptr<Operation> op) = 0;
};

} // namespace neurus
