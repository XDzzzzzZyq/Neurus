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

#include <string>

#include "glm/glm.hpp"

#include "editor/events/SceneEvents.h"
#include "editor/operations/Operation.h"

namespace neurus {

/** @brief Viewport + render visibility pair (value carried by SetVisibilityOp). */
struct VisibilityState
{
	bool viewportVisible = true;
	bool renderVisible = true;
};

/** @brief Camera pose endpoint: position + look-at target (value for CameraTransformOp). */
struct CameraPose
{
	glm::vec3 position{ 0.0f };
	glm::vec3 target{ 0.0f };
};

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

/** @brief Absolute visibility edit (viewport + render flags). */
class SetVisibilityOp : public TransitionOp<SetVisibilityOp, VisibilityChanged, VisibilityState>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Visibility";

	VisibilityChanged MakeEvent(const ObjectID* o, const VisibilityState& v) const
	{
		return VisibilityChanged{ o, v.viewportVisible, v.renderVisible };
	}
};

/** @brief Absolute light-radius edit. */
class SetLightRadiusOp : public TransitionOp<SetLightRadiusOp, LightRadiusChanged, float>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Light Radius";

	LightRadiusChanged MakeEvent(const ObjectID* o, const float& v) const
	{
		return LightRadiusChanged{ o, v };
	}
};

/** @brief Absolute spot-light cutoff edit. */
class SetLightCutoffOp : public TransitionOp<SetLightCutoffOp, LightCutoffChanged, float>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Light Cutoff";

	LightCutoffChanged MakeEvent(const ObjectID* o, const float& v) const
	{
		return LightCutoffChanged{ o, v };
	}
};

/** @brief Absolute spot-light outer-cutoff edit. */
class SetLightOuterCutoffOp : public TransitionOp<SetLightOuterCutoffOp, LightOuterCutoffChanged, float>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Light Outer Cutoff";

	LightOuterCutoffChanged MakeEvent(const ObjectID* o, const float& v) const
	{
		return LightOuterCutoffChanged{ o, v };
	}
};

/** @brief Absolute mesh shadow-casting toggle. */
class SetMeshShadowOp : public TransitionOp<SetMeshShadowOp, MeshShadowChanged, bool>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Toggle Mesh Shadow";

	MeshShadowChanged MakeEvent(const ObjectID* o, const bool& v) const
	{
		return MeshShadowChanged{ o, v };
	}
};

/** @brief Absolute mesh material toggle. */
class SetMeshMaterialOp : public TransitionOp<SetMeshMaterialOp, MeshMaterialChanged, bool>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Toggle Mesh Material";

	MeshMaterialChanged MakeEvent(const ObjectID* o, const bool& v) const
	{
		return MeshMaterialChanged{ o, v };
	}
};

/** @brief Absolute environment IBL-intensity edit. */
class SetEnvIntensityOp : public TransitionOp<SetEnvIntensityOp, EnvironmentIntensityChanged, float>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Environment Intensity";

	EnvironmentIntensityChanged MakeEvent(const ObjectID* o, const float& v) const
	{
		return EnvironmentIntensityChanged{ o, v };
	}
};

/** @brief Absolute environment rotation edit. */
class SetEnvRotationOp : public TransitionOp<SetEnvRotationOp, EnvironmentRotationChanged, float>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Set Environment Rotation";

	EnvironmentRotationChanged MakeEvent(const ObjectID* o, const float& v) const
	{
		return EnvironmentRotationChanged{ o, v };
	}
};

/**
 * @brief Absolute camera pose edit (position + target), coarse-grained.
 *
 * Records the coupled camera transform for viewport navigation (orbit / pan /
 * dolly / zoom) and panel target edits. A non-empty, per-camera MergeKey folds
 * a continuous manipulation into one undo step. Camera *position* edited via the
 * property panel reuses the generic SetPositionOp, so there is no separate
 * camera-position op to overlap with the object transform path.
 */
class CameraTransformOp : public TransitionOp<CameraTransformOp, CameraPoseChanged, CameraPose>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Camera Transform";

	std::string MergeKey() const override { return "camera_pose:" + std::to_string(m_uid); }

	CameraPoseChanged MakeEvent(const ObjectID* o, const CameraPose& v) const
	{
		return CameraPoseChanged{ o,
			v.position.x, v.position.y, v.position.z,
			v.target.x, v.target.y, v.target.z };
	}
};

/** @brief Absolute camera field-of-view ("Camera Ratio") edit. */
class CameraFovOp : public TransitionOp<CameraFovOp, CameraFovChanged, float>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Camera Ratio";

	std::string MergeKey() const override { return "camera_fov:" + std::to_string(m_uid); }

	CameraFovChanged MakeEvent(const ObjectID* o, const float& v) const
	{
		return CameraFovChanged{ o, v };
	}
};

} // namespace neurus
