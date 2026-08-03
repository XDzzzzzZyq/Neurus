/**
 * @file OperationManager.h
 * @brief Undo/redo history over event-replay operations (Design B).
 *
 * Holds two stacks of forward operations. Undo pops the top forward op `g`,
 * emits `g.Inverse()` synchronously (which re-runs the controller handler and
 * re-applies the mutation), and pushes the inverse onto the redo stack. Redo
 * is symmetric: it emits `(g⁻¹).Inverse() = g`.
 *
 * Replay is synchronous and guarded by Phase::Replaying, so Submit() (called
 * by the re-run handler) is suppressed and does not corrupt the stacks. This
 * is only safe because Emit() dispatches via EventQueue::EmitNow (direct
 * dispatch), never through the deferred queue.
 *
 * The manager does not own the Scene: it holds a provider so the correct
 * (possibly swapped) scene is resolved at replay time. Implements
 * IOperationSink so controllers can record without knowing the concrete type.
 */

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "editor/operations/IOperationSink.h"
#include "editor/operations/Operation.h"

namespace neurus {

class EventQueue;
class Scene;

/**
 * @brief Owns the undo/redo history and drives synchronous replay.
 */
class OperationManager : public IOperationSink
{
public:
	/**
	 * @brief Constructs the manager.
	 * @param bus Event queue operations replay their events on.
	 * @param sceneProvider Returns the current scene (re-queried each replay,
	 *        so a scene swap does not leave a dangling reference).
	 */
	OperationManager(EventQueue& bus, std::function<Scene*()> sceneProvider);

	// --- IOperationSink ---
	void Submit(std::unique_ptr<Operation> op) override;

	// --- History control ---
	/** @brief Undoes the most recent operation (no-op if none). */
	void Undo();
	/** @brief Redoes the most recently undone operation (no-op if none). */
	void Redo();

	bool CanUndo() const { return !m_undo.empty(); }
	bool CanRedo() const { return !m_redo.empty(); }

	/** @brief Clears both stacks (e.g. on New/Load scene). */
	void Clear();

private:
	/** @brief Replay phase — Submit() is suppressed while Replaying. */
	enum class Phase { Idle, Replaying };

	/** @brief Emits an operation synchronously under the replay guard. */
	void Replay(Operation& op);

	EventQueue& m_bus;
	std::function<Scene*()> m_sceneProvider;

	std::vector<std::unique_ptr<Operation>> m_undo;
	std::vector<std::unique_ptr<Operation>> m_redo;

	Phase m_phase = Phase::Idle;
};

} // namespace neurus
