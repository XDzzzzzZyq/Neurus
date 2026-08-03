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

namespace neurus {

struct OperationContext;

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

} // namespace neurus
