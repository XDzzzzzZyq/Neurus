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

protected:
	int m_uid;      ///< Target object UID (serialized).
	Value m_before; ///< Value before the edit (serialized).
	Value m_after;  ///< Value after the edit (serialized).
};

} // namespace neurus
