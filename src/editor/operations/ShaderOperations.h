/**
 * @file ShaderOperations.h
 * @brief Undoable operations for per-mesh shader content edits (delta-only).
 *
 * Shader content edits are per-object state (a mesh's shader stage), so these
 * ops are UID-keyed like the TransitionOp family. Rather than snapshotting the
 * whole stage source, each op carries only the delta for one edit dimension,
 * mirroring the granularity of the forward edit events:
 *
 *   - SetShaderCodeOp   : ShaderCodeEdited      -> before/after GLSL text
 *   - SetShaderFieldOp   : ShaderStructEdited    -> before/after of one element
 *   - AddShaderFieldOp   : ShaderFieldAdded      -> append/remove one default entry
 *
 * This keeps the serialized history compact: a field edit stores the two
 * endpoints of one ShaderStruct element (not two whole-IR copies), and a
 * field-add stores nothing but a section + index + direction flag.
 *
 * Replay dispatches a dedicated restore event (ShaderCodeRestored /
 * ShaderFieldRestored / ShaderFieldAddRestored / ShaderFieldRemoved) so mutation
 * stays on the single ShaderController path AND the ShaderUnit version bumps only
 * on undo/redo (forward live edits deliberately skip the bump to avoid a
 * cursor-jumping panel reload mid-typing).
 *
 * Scope: content edits only. Shader Create/Compile remain non-undoable lifecycle
 * actions. Replay restores CPU-side source only and bumps the ShaderUnit version
 * so the editor panel refreshes; it does NOT recompile to SPIR-V (the user
 * presses Compile to push to the GPU), matching the "content edits don't compile"
 * model.
 *
 * Edits are deliberately NOT mergeable (empty MergeKey): a code-edit burst is
 * bracketed by the controller's ShaderEditBegin/ShaderEditEnd gesture and
 * recorded once on focus-out; discrete struct/field edits record one op each.
 */

#pragma once

#include <memory>
#include <string>
#include <utility>

#include "editor/events/ShaderEvents.h"
#include "editor/operations/Operation.h"
#include "render/shaders/ShaderStructSerialize.h"

namespace neurus {

/**
 * @brief Undoable edit of one shader stage's GLSL code text (before -> after).
 *
 * Carries only the two text endpoints; no parsed IR. Replays by dispatching
 * ShaderCodeRestored, which overwrites ShaderUnit::code and bumps the version.
 */
class SetShaderCodeOp : public Operation
{
public:
	SetShaderCodeOp() = default;

	SetShaderCodeOp(int uid, int stage, std::string beforeCode, std::string afterCode)
		: m_uid(uid)
		, m_stage(stage)
		, m_beforeCode(std::move(beforeCode))
		, m_afterCode(std::move(afterCode))
	{}

	void Apply(OperationContext& ctx) override
	{
		// Replayed event carries the mesh UID; ShaderController resolves it
		// against the current scene and no-ops stale ids.
		ctx.events.emitNow(ShaderCodeRestored{ m_uid, m_stage, m_afterCode });
	}

	std::unique_ptr<Operation> Inverse() const override
	{
		return std::make_unique<SetShaderCodeOp>(m_uid, m_stage, m_afterCode, m_beforeCode);
	}

	std::string Label() const override { return "Edit Shader Code"; }

	/** @brief Serializes {uid, stage, beforeCode, afterCode}. */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("uid", m_uid),
		   cereal::make_nvp("stage", m_stage),
		   cereal::make_nvp("beforeCode", m_beforeCode),
		   cereal::make_nvp("afterCode", m_afterCode));
	}

private:
	int         m_uid = 0;     ///< Target mesh UID (serialized).
	int         m_stage = 0;   ///< ShaderType as int (serialized).
	std::string m_beforeCode;  ///< GLSL text before the edit (serialized).
	std::string m_afterCode;   ///< GLSL text after the edit (serialized).
};

/**
 * @brief Undoable edit of one ShaderStruct element (before -> after).
 *
 * Identifies the section + entry index and carries the whole addressed element
 * (a ShaderFieldValue variant) at both endpoints. Replays by dispatching
 * ShaderFieldRestored, which assigns the element back into the live IR and bumps
 * the version. Storing the whole element (not a stringified {field,value}) keeps
 * inversion a plain endpoint swap and reuses the future shader-IR serialization.
 */
class SetShaderFieldOp : public Operation
{
public:
	SetShaderFieldOp() = default;

	SetShaderFieldOp(int uid, int stage, ShaderSection section, int fieldIndex,
	                 ShaderFieldValue before, ShaderFieldValue after)
		: m_uid(uid)
		, m_stage(stage)
		, m_section(section)
		, m_fieldIndex(fieldIndex)
		, m_before(std::move(before))
		, m_after(std::move(after))
	{}

	void Apply(OperationContext& ctx) override
	{
		ctx.events.emitNow(ShaderFieldRestored{
			m_uid, m_stage, m_section, m_fieldIndex, m_after });
	}

	std::unique_ptr<Operation> Inverse() const override
	{
		return std::make_unique<SetShaderFieldOp>(
			m_uid, m_stage, m_section, m_fieldIndex, m_after, m_before);
	}

	std::string Label() const override { return "Edit Shader Field"; }

	/** @brief Serializes {uid, stage, section, fieldIndex, before, after}. */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("uid", m_uid),
		   cereal::make_nvp("stage", m_stage),
		   cereal::make_nvp("section", m_section),
		   cereal::make_nvp("fieldIndex", m_fieldIndex),
		   cereal::make_nvp("before", m_before),
		   cereal::make_nvp("after", m_after));
	}

private:
	int              m_uid = 0;        ///< Target mesh UID (serialized).
	int              m_stage = 0;      ///< ShaderType as int (serialized).
	ShaderSection    m_section{};      ///< Which ShaderStruct container (serialized).
	int              m_fieldIndex = 0; ///< Index into the section's vector (serialized).
	ShaderFieldValue m_before;         ///< Whole element before the edit (serialized).
	ShaderFieldValue m_after;          ///< Whole element after the edit (serialized).
};

/**
 * @brief Undoable append of one default entry to a ShaderStruct container.
 *
 * Carries the section + sub-index and a direction flag. Because undo/redo is
 * strictly LIFO, the added entry is always the last element, so undo simply
 * removes the last entry and redo re-appends an identical default.
 *
 *   m_add == true  -> Apply dispatches ShaderFieldAddRestored (re-append default)
 *   m_add == false -> Apply dispatches ShaderFieldRemoved     (drop last entry)
 *
 * Inverse flips the flag, making the op its own logical inverse.
 */
class AddShaderFieldOp : public Operation
{
public:
	AddShaderFieldOp() = default;

	AddShaderFieldOp(int uid, int stage, ShaderSection section, int subFieldIndex, bool add)
		: m_uid(uid)
		, m_stage(stage)
		, m_section(section)
		, m_subFieldIndex(subFieldIndex)
		, m_add(add)
	{}

	void Apply(OperationContext& ctx) override
	{
		if (m_add)
			ctx.events.emitNow(ShaderFieldAddRestored{ m_uid, m_stage, m_section, m_subFieldIndex });
		else
			ctx.events.emitNow(ShaderFieldRemoved{ m_uid, m_stage, m_section, m_subFieldIndex });
	}

	std::unique_ptr<Operation> Inverse() const override
	{
		return std::make_unique<AddShaderFieldOp>(m_uid, m_stage, m_section, m_subFieldIndex, !m_add);
	}

	std::string Label() const override { return "Add Shader Field"; }

	/** @brief Serializes {uid, stage, section, subFieldIndex, add}. */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("uid", m_uid),
		   cereal::make_nvp("stage", m_stage),
		   cereal::make_nvp("section", m_section),
		   cereal::make_nvp("subFieldIndex", m_subFieldIndex),
		   cereal::make_nvp("add", m_add));
	}

private:
	int           m_uid = 0;             ///< Target mesh UID (serialized).
	int           m_stage = 0;           ///< ShaderType as int (serialized).
	ShaderSection m_section{};           ///< Which ShaderStruct container (serialized).
	int           m_subFieldIndex = -1;  ///< StructDefs target struct-def index (-1 = top-level) (serialized).
	bool          m_add = true;          ///< true = append default, false = remove last (serialized).
};

} // namespace neurus
