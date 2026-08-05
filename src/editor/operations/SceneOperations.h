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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cereal/types/vector.hpp>

#include "glm/glm.hpp"

#include "editor/events/SceneEvents.h"
#include "editor/operations/Operation.h"
#include "scene/GlmSerialization.h"

namespace neurus {

/** @brief Viewport + render visibility pair (value carried by SetVisibilityOp). */
struct VisibilityState
{
	bool viewportVisible = true;
	bool renderVisible = true;

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("viewportVisible", viewportVisible),
		   cereal::make_nvp("renderVisible", renderVisible));
	}
};

/** @brief Camera pose endpoint: position + look-at target (value for CameraTransformOp). */
struct CameraPose
{
	glm::vec3 position{ 0.0f };
	glm::vec3 target{ 0.0f };

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("position", position),
		   cereal::make_nvp("target", target));
	}
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
 * Records the coupled camera transform for an orbit / pan / dolly drag and for
 * panel target edits. Non-mergeable (empty MergeKey): each recorded op is its
 * own undo entry. This is correct because a viewport drag is already bounded by
 * a controller gesture (CameraDragBegin/End) that commits exactly ONE op on
 * release — so consecutive separate drags must stay separate undo entries.
 *
 * Scroll zoom, which has no press/release boundary, uses the mergeable sibling
 * CameraZoomOp instead. Camera *position* edited via the property panel reuses
 * the generic SetPositionOp, so there is no separate camera-position op to
 * overlap with the object transform path.
 */
class CameraTransformOp : public TransitionOp<CameraTransformOp, CameraPoseChanged, CameraPose>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Camera Transform";

	CameraPoseChanged MakeEvent(const ObjectID* o, const CameraPose& v) const
	{
		return CameraPoseChanged{ o,
			v.position.x, v.position.y, v.position.z,
			v.target.x, v.target.y, v.target.z };
	}
};

/**
 * @brief Absolute camera pose edit produced by scroll zoom — mergeable.
 *
 * Identical replay semantics to CameraTransformOp (same absolute CameraPose,
 * same CameraPoseChanged event), but exposes a per-camera MergeKey so a scroll
 * burst — which fires one op per notch with no gesture boundary — coalesces
 * into a single undo entry. Kept a separate type (rather than a flag on
 * CameraTransformOp) so the mergeable-vs-standalone intent is encoded in the
 * type, not decided at each call site.
 */
class CameraZoomOp : public TransitionOp<CameraZoomOp, CameraPoseChanged, CameraPose>
{
public:
	using TransitionOp::TransitionOp;
	static constexpr const char* kLabel = "Camera Zoom";

	std::string MergeKey() const override { return "camera_zoom:" + std::to_string(m_uid); }

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

/** @brief Absolute selection-set endpoint: ordered selected UIDs + active UID. */
struct SelectionState
{
	std::vector<int> selectedUids; ///< Ordered selected object UIDs.
	int activeUid = 0;             ///< Active object UID (0 = none).

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("selectedUids", selectedUids),
		   cereal::make_nvp("activeUid", activeUid));
	}
};

/**
 * @brief Absolute selection-set edit (select / multi-select / deselect / clear).
 *
 * Selection is scene-level SET state (Scene::selections), not a per-object
 * flag, so it does not fit TransitionOp: the op stores the full before/after
 * UID lists and replays by dispatching a single SelectionChanged event.
 *
 * PreservesRedo() is true so navigating the selection does NOT discard a
 * pending redo chain. This is safe because every operation is an absolute
 * state-set: a preserved redo op restores its stored end state regardless of
 * selection changes recorded in between.
 */
class SetSelectionOp : public Operation
{
public:
	SetSelectionOp() = default;

	SetSelectionOp(SelectionState before, SelectionState after)
		: m_before(std::move(before))
		, m_after(std::move(after))
	{}

	void Emit(OperationContext& ctx) override
	{
		ctx.bus.EmitNow(SelectionChanged{ &ctx.scene, m_after.selectedUids, m_after.activeUid });
	}

	std::unique_ptr<Operation> Inverse() const override
	{
		return std::make_unique<SetSelectionOp>(m_after, m_before);
	}

	std::string Label() const override { return "Select"; }

	bool PreservesRedo() const override { return true; }

	/** @brief Serializes the before/after selection endpoints. */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("before", m_before),
		   cereal::make_nvp("after", m_after));
	}

private:
	SelectionState m_before;
	SelectionState m_after;
};

} // namespace neurus
