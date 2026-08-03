/**
 * @file SceneOperations.h
 * @brief Concrete undoable operations for scene property/transform edits.
 *
 * All Phase 1 edits are "absolute set" operations: the operation stores the
 * target object's UID plus the absolute before/after values, and replays by
 * dispatching the same scene event the UI would emit. This keeps mutation on
 * the single controller path and makes inversion a trivial value swap.
 *
 * AbsoluteSetOperation<TEvent, Value> is generic over the event type and the
 * stored value; a factory turns (object, value) into the concrete event so a
 * vec3 can fan out to a 3-float event (e.g. PositionChanged). The Make*
 * helpers bind the label + factory for each edit kind, so controllers record
 * with a single call.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "glm/glm.hpp"

#include "editor/events/SceneEvents.h"
#include "editor/operations/Operation.h"
#include "editor/operations/OperationContext.h"
#include "scene/UID.h"

namespace neurus {

/**
 * @brief Absolute-set operation: replays TEvent(object, value) by UID.
 * @tparam TEvent Scene event struct dispatched on replay.
 * @tparam Value  Stored value type (float, bool, glm::vec3, ...).
 */
template<typename TEvent, typename Value>
class AbsoluteSetOperation : public Operation
{
public:
	/** @brief Builds the concrete event from a resolved object + value. */
	using Factory = std::function<TEvent(const ObjectID*, const Value&)>;

	AbsoluteSetOperation(std::string label, Factory factory,
	                     int uid, Value before, Value after)
		: m_label(std::move(label))
		, m_factory(std::move(factory))
		, m_uid(uid)
		, m_before(std::move(before))
		, m_after(std::move(after))
	{}

	void Emit(OperationContext& ctx) override
	{
		const ObjectID* obj = ctx.Resolve(m_uid);
		if (!obj) return; // Stale identity: object gone, safe no-op.
		ctx.bus.EmitNow(m_factory(obj, m_after));
	}

	std::unique_ptr<Operation> Inverse() const override
	{
		// Swap before/after — involution holds by construction.
		return std::make_unique<AbsoluteSetOperation>(
			m_label, m_factory, m_uid, m_after, m_before);
	}

	std::string Label() const override { return m_label; }

private:
	std::string m_label;
	Factory m_factory;
	int m_uid;
	Value m_before;
	Value m_after;
};

// ---------------------------------------------------------------------------
// Make helpers — bind label + event factory for each edit kind.
// ---------------------------------------------------------------------------

/** @brief Records an absolute light-power edit. */
inline std::unique_ptr<Operation> MakeSetPower(int uid, float before, float after)
{
	return std::make_unique<AbsoluteSetOperation<LightPowerChanged, float>>(
		"Set Light Power",
		[](const ObjectID* o, const float& v) { return LightPowerChanged{ o, v }; },
		uid, before, after);
}

/** @brief Records an absolute light-color edit. */
inline std::unique_ptr<Operation> MakeSetColor(int uid, glm::vec3 before, glm::vec3 after)
{
	return std::make_unique<AbsoluteSetOperation<LightColorChanged, glm::vec3>>(
		"Set Light Color",
		[](const ObjectID* o, const glm::vec3& v) {
			return LightColorChanged{ o, v.r, v.g, v.b };
		},
		uid, before, after);
}

/** @brief Records an absolute light-shadow toggle. */
inline std::unique_ptr<Operation> MakeSetShadow(int uid, bool before, bool after)
{
	return std::make_unique<AbsoluteSetOperation<LightShadowChanged, bool>>(
		"Toggle Light Shadow",
		[](const ObjectID* o, const bool& v) { return LightShadowChanged{ o, v }; },
		uid, before, after);
}

/** @brief Records an absolute position edit. */
inline std::unique_ptr<Operation> MakeSetPosition(int uid, glm::vec3 before, glm::vec3 after)
{
	return std::make_unique<AbsoluteSetOperation<PositionChanged, glm::vec3>>(
		"Move",
		[](const ObjectID* o, const glm::vec3& v) {
			return PositionChanged{ o, v.x, v.y, v.z };
		},
		uid, before, after);
}

/** @brief Records an absolute rotation edit. */
inline std::unique_ptr<Operation> MakeSetRotation(int uid, glm::vec3 before, glm::vec3 after)
{
	return std::make_unique<AbsoluteSetOperation<RotationChanged, glm::vec3>>(
		"Rotate",
		[](const ObjectID* o, const glm::vec3& v) {
			return RotationChanged{ o, v.x, v.y, v.z };
		},
		uid, before, after);
}

/** @brief Records an absolute scale edit. */
inline std::unique_ptr<Operation> MakeSetScale(int uid, glm::vec3 before, glm::vec3 after)
{
	return std::make_unique<AbsoluteSetOperation<ScaleChanged, glm::vec3>>(
		"Scale",
		[](const ObjectID* o, const glm::vec3& v) {
			return ScaleChanged{ o, v.x, v.y, v.z };
		},
		uid, before, after);
}

} // namespace neurus
