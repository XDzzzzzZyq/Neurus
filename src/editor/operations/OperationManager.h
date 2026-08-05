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
 * is only safe because Apply() dispatches via EventQueue::emitNow (direct
 * dispatch), never through the deferred queue.
 *
 * The manager does not own the Scene: it holds a provider so the correct
 * (possibly swapped) scene is resolved at replay time. Implements
 * IOperationSink so controllers can record without knowing the concrete type.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "editor/operations/HistoryView.h"
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
	/** @brief Default cap on undo-stack depth (per Issue #19). */
	static constexpr size_t kDefaultMaxUndoDepth = 256;

	/**
	 * @brief Constructs the manager.
	 * @param bus Event queue operations replay their events on.
	 * @param sceneProvider Returns the current scene (re-queried each replay,
	 *        so a scene swap does not leave a dangling reference).
	 * @param maxUndoDepth Cap on undo-stack entries; oldest are evicted past it
	 *        (default kDefaultMaxUndoDepth). Bounds memory for long sessions.
	 */
	OperationManager(EventQueue& bus, std::function<Scene*()> sceneProvider,
	                 size_t maxUndoDepth = kDefaultMaxUndoDepth);

	// --- IOperationSink ---
	void Submit(std::unique_ptr<Operation> op) override;

	// --- History control ---
	/** @brief Undoes the most recent operation (no-op if none). */
	void Undo();
	/** @brief Redoes the most recently undone operation (no-op if none). */
	void Redo();

	bool CanUndo() const { return !m_undo.empty(); }
	bool CanRedo() const { return !m_redo.empty(); }

	/**
	 * @brief Builds a read-only snapshot of both stacks for the History panel.
	 * @return Labels (oldest-first undo, replay-order redo) plus the current revision.
	 */
	HistoryView GetHistoryView() const;

	/** @brief Clears both stacks (e.g. on New/Load scene). */
	void Clear();

	// --- Persistence hooks (used by project::HistoryComponent) ---
	/** @brief Undo stack, oldest→newest (next-undo last). Read-only. */
	const std::vector<std::unique_ptr<Operation>>& GetUndoStack() const { return m_undo; }
	/** @brief Redo stack, bottom→top (next-redo last). Read-only. */
	const std::vector<std::unique_ptr<Operation>>& GetRedoStack() const { return m_redo; }

	/**
	 * @brief Replaces both stacks wholesale (project load).
	 * @param undo New undo stack (ownership taken).
	 * @param redo New redo stack (ownership taken).
	 * @note Does NOT replay: the ops are absolute state-sets restored as-is;
	 *       the live scene is loaded separately. Bumps the revision so the UI
	 *       refreshes its history view.
	 */
	void RestoreHistory(std::vector<std::unique_ptr<Operation>> undo,
	                    std::vector<std::unique_ptr<Operation>> redo);

private:
	/** @brief Replay phase — Submit() is suppressed while Replaying. */
	enum class Phase { Idle, Replaying };

	/** @brief Emits an operation synchronously under the replay guard. */
	void Replay(Operation& op);

	/**
	 * @brief Evicts the oldest undo entries so the stack stays within
	 *        m_maxUndoDepth. Redo is unaffected (bounded implicitly by undo).
	 */
	void EnforceUndoLimit();

	EventQueue& m_bus;
	std::function<Scene*()> m_sceneProvider;

	std::vector<std::unique_ptr<Operation>> m_undo;
	std::vector<std::unique_ptr<Operation>> m_redo;

	/// Maximum undo-stack depth; oldest entries evicted past this.
	size_t m_maxUndoDepth = kDefaultMaxUndoDepth;

	/// Monotonic counter bumped on every stack change; drives UI change detection.
	uint64_t m_revision = 0;

	Phase m_phase = Phase::Idle;
};

} // namespace neurus
