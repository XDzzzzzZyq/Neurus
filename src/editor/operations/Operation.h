/**
 * @file Operation.h
 * @brief Abstract undoable operation (ID-based inverse descriptor).
 *
 * An Operation is a value-only description of a scene edit. It never mutates
 * the scene directly: Emit() dispatches this operation's absolute-set event
 * synchronously on the EventQueue, so the existing controller handler performs
 * the actual mutation (single mutation path).
 *
 * Undo/redo is group-theoretic (Design B): the undo stack holds the forward
 * operation `g`; undo emits `g.Inverse()` and pushes the inverse to redo; redo
 * emits `(g⁻¹).Inverse() = g`. This requires Inverse() to be an involution:
 * `Inverse()∘Inverse()` must yield an operation equivalent to the original.
 *
 * Operations store a scene object's integer UID (not a raw pointer) plus
 * absolute before/after values, so they survive across scene mutations and
 * safely no-op when the referenced object no longer exists (stale identity).
 */

#pragma once

#include <memory>
#include <string>
#include <utility>

#include "editor/operations/OperationContext.h"

namespace neurus {

/**
 * @brief Base class for all undoable operations.
 */
class Operation
{
public:
	virtual ~Operation() = default;

	/**
	 * @brief Dispatches this operation's absolute-set event synchronously.
	 * @param ctx Replay context (scene for UID resolution + event queue).
	 * @note Must resolve its target by UID and no-op if the object is gone.
	 */
	virtual void Emit(OperationContext& ctx) = 0;

	/**
	 * @brief Returns a fresh operation with before/after values swapped.
	 * @return Owning pointer to the inverse operation.
	 * @note Involution: Inverse()->Inverse() must equal the original edit.
	 */
	virtual std::unique_ptr<Operation> Inverse() const = 0;

	/**
	 * @brief Human-readable label (for Edit menu / debugging).
	 */
	virtual std::string Label() const = 0;

	/**
	 * @brief Coalescing key for merging consecutive edits into one history entry.
	 * @return Non-empty key for mergeable ops, empty (default) for standalone ops.
	 * @note Two ops merge only when their keys are equal and non-empty; encode the
	 *       target UID in the key so edits to different objects never coalesce.
	 *       Used for low-frequency, high-count streams (camera scroll/drag) so a
	 *       continuous manipulation collapses to a single undo step.
	 */
	virtual std::string MergeKey() const { return {}; }

	/**
	 * @brief Folds a newer same-key op's end state into this op (extends the range).
	 * @param newer The just-submitted op that shares this op's MergeKey().
	 * @note Only called by OperationManager when MergeKey() matches, which
	 *       guarantees @p newer is the same concrete type and target. The base
	 *       does nothing; TransitionOp adopts the newer "after" value.
	 */
	virtual void MergeFrom(const Operation& newer) { (void)newer; }

	/**
	 * @brief Whether recording this op should keep the redo stack intact.
	 * @return false (default) for edits that branch history and clear redo;
	 *         true for "transparent" ops that append to undo without discarding
	 *         a pending redo chain.
	 * @note Safe only because operations are absolute state-sets (before/after
	 *       endpoints by UID), not deltas: a preserved redo op replays to its
	 *       stored end state correctly regardless of any transparent ops recorded
	 *       in between. Selection changes use this so navigating the selection
	 *       does not throw away an undone edit the user may still redo.
	 */
	virtual bool PreservesRedo() const { return false; }
};

/**
 * @brief Generic base for reversible before→after value edits (a "transition").
 *
 * A transition stores a target object's UID plus the *absolute* before/after
 * values (endpoints, not deltas), and replays by dispatching the scene event
 * the UI would emit — keeping mutation on the single controller path and making
 * inversion a value swap. This base carries all the shared logic; a concrete
 * subclass only declares how a stored value becomes its event via a
 * `MakeEvent()` member and a static `kLabel`.
 *
 * CRTP (Curiously Recurring Template Pattern) lets Inverse() reconstruct the
 * *concrete* subclass with swapped values, so an operation on the redo stack
 * has the same type — and, in the future, the same serialized identity — as a
 * freshly recorded one. The only per-operation state is {uid, before, after};
 * there is no stored function object, so an operation is trivially serializable.
 *
 * @tparam Derived Concrete subclass (CRTP); must provide
 *                 `TEvent MakeEvent(const ObjectID*, const Value&) const` and
 *                 `static constexpr const char* kLabel`.
 * @tparam TEvent  Scene event struct dispatched on replay.
 * @tparam Value   Stored value type (float, bool, glm::vec3, ...).
 */
template<typename Derived, typename TEvent, typename Value>
class TransitionOp : public Operation
{
public:
	TransitionOp(int uid, Value before, Value after)
		: m_uid(uid)
		, m_before(std::move(before))
		, m_after(std::move(after))
	{}

	void Emit(OperationContext& ctx) override
	{
		const ObjectID* obj = ctx.Resolve(m_uid);
		if (!obj) return; // Stale identity: object gone, safe no-op.
		ctx.bus.EmitNow(static_cast<const Derived*>(this)->MakeEvent(obj, m_after));
	}

	std::unique_ptr<Operation> Inverse() const override
	{
		// Swap before/after and rebuild the concrete type — involution holds.
		return std::make_unique<Derived>(m_uid, m_after, m_before);
	}

	std::string Label() const override { return Derived::kLabel; }

	/**
	 * @brief Adopts @p newer's "after" value, keeping this op's original "before".
	 * @note Safe cast: OperationManager calls this only when MergeKey() matches,
	 *       so @p newer is the same TransitionOp specialization.
	 */
	void MergeFrom(const Operation& newer) override
	{
		m_after = static_cast<const TransitionOp&>(newer).m_after;
	}

protected:
	int m_uid;      ///< Target object UID (serialized).
	Value m_before; ///< Value before the edit (serialized).
	Value m_after;  ///< Value after the edit (serialized).
};

} // namespace neurus
