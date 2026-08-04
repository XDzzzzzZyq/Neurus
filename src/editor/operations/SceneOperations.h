/**
 * @file SceneOperations.h
 * @brief Concrete undoable operations for scene property/transform edits.
 *
 * Each edit is a reversible before→after transition built on TransitionOp
 * (see Operation.h): it stores the target object's UID plus absolute
 * before/after values, and replays by dispatching the same scene event the UI
 * would emit. A concrete class supplies only two things — a MakeEvent() that
 * turns a stored value into its event, and a kLabel — while the CRTP base
 * provides Emit/Inverse/Label. Controllers record an edit by constructing the
 * matching operation directly (std::make_unique<SetScaleOp>(...)).
 */

#pragma once

#include "glm/glm.hpp"

#include "editor/events/SceneEvents.h"
#include "editor/operations/Operation.h"

namespace neurus {

/** @brief Absolute light-power edit. */
class SetLightPowerOp : public TransitionOp<SetLightPowerOp, LightPowerChanged, float>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Light Power";

	LightPowerChanged MakeEvent(const ObjectID* o, const float& v) const
	{
		return LightPowerChanged{ o, v };
	}
};

/** @brief Absolute light-color edit. */
class SetLightColorOp : public TransitionOp<SetLightColorOp, LightColorChanged, glm::vec3>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Light Color";

	LightColorChanged MakeEvent(const ObjectID* o, const glm::vec3& v) const
	{
		return LightColorChanged{ o, v.r, v.g, v.b };
	}
};

/** @brief Absolute light-shadow toggle. */
class SetLightShadowOp : public TransitionOp<SetLightShadowOp, LightShadowChanged, bool>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Toggle Light Shadow";

	LightShadowChanged MakeEvent(const ObjectID* o, const bool& v) const
	{
		return LightShadowChanged{ o, v };
	}
};

/** @brief Absolute position edit. */
class SetPositionOp : public TransitionOp<SetPositionOp, PositionChanged, glm::vec3>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Move";

	PositionChanged MakeEvent(const ObjectID* o, const glm::vec3& v) const
	{
		return PositionChanged{ o, v.x, v.y, v.z };
	}
};

/** @brief Absolute rotation edit. */
class SetRotationOp : public TransitionOp<SetRotationOp, RotationChanged, glm::vec3>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Rotate";

	RotationChanged MakeEvent(const ObjectID* o, const glm::vec3& v) const
	{
		return RotationChanged{ o, v.x, v.y, v.z };
	}
};

/** @brief Absolute scale edit. */
class SetScaleOp : public TransitionOp<SetScaleOp, ScaleChanged, glm::vec3>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Scale";

	ScaleChanged MakeEvent(const ObjectID* o, const glm::vec3& v) const
	{
		return ScaleChanged{ o, v.x, v.y, v.z };
	}
};

} // namespace neurus
