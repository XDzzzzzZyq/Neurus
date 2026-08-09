/**
 * @file ShaderEvents.h
 * @brief Event structs for the shader editor pipeline (UI -> Editor -> ShaderController).
 *
 * All events carry an int objectUid (the target mesh's UID, resolved once by
 * the ShaderEditorPanel from the active scene selection). The ShaderController
 * resolves the id against the current scene and casts to Mesh* (in the .cpp
 * only) and operates on the shader data directly.
 *
 * Architecture:
 * - Pure data structs, no Qt headers, no Vulkan headers.
 * - ShaderSection enum identifies which ShaderStruct container is being edited.
 * - Events are enqueued via Editor::OnUIEvent -> EventQueue -> ShaderController.
 *
 * Version flow:
 * - Only ShaderCreateRequested and ShaderCompileRequested bump Shader::m_version
 *   (and only on successful compile).
 * - ShaderCodeEdited and ShaderStructEdited do NOT bump version — the user
 *   must press Compile to apply changes to the GPU pipeline.
 */

#pragma once

#include <string>
#include <variant>

#include "render/shaders/ShaderStruct.h"

namespace neurus {

/**
 * @brief One ShaderStruct element payload (the granularity of an undoable edit).
 *
 * A struct edit captures the whole addressed element before and after the edit
 * (not a stringified single field), so undo/redo restores it by plain
 * assignment and the op serializes via the non-intrusive serialize functions in
 * ShaderStructSerialize.h. The ShaderSection still discriminates *which*
 * container the element belongs to (multiple sections share an element type:
 * AB_list/pass_list are both S_IO; input/output/uniform are all S_Uniform).
 */
using ShaderFieldValue = std::variant<S_IO, S_Uniform, S_Func, S_PushConstant, S_StructDef>;

/**
 * @brief Identifies which ShaderStruct container is being edited.
 *
 * Maps 1:1 to the public vectors in ShaderStruct (ShaderStruct.h).
 * Used by ShaderStructEdited::section to dispatch the edit to the right list.
 */
enum class ShaderSection : int
{
	Attributes    = 0,  ///< AB_list          (S_IO)         — vertex attributes
	PassOutputs   = 1,  ///< pass_list        (S_IO)         — render-pass outputs
	Inputs        = 2,  ///< input_list       (S_Uniform)    — shader-stage inputs
	Outputs       = 3,  ///< output_list      (S_Uniform)    — shader-stage outputs
	Uniforms      = 4,  ///< uniform_list     (S_Uniform)    — single-value uniforms
	StructDefs    = 5,  ///< struct_def_list  (S_StructDef)  — bare struct definitions
	Functions     = 6,  ///< func_list        (S_Func)        — user-defined functions
	PushConstants = 7,  ///< push_constants   (S_PushConstant)— push-constant members
};

/**
 * @brief Emitted when the user clicks "Create Shader" for a mesh with no shader.
 *
 * ShaderController loads default gbuffer shaders, parses, generates, compiles
 * all stages, and assigns the result to mesh->o_shader. Bumps version on success.
 */
struct ShaderCreateRequested
{
	int objectUid = 0;
};

/**
 * @brief Emitted when the user edits the GLSL code text for a stage.
 *
 * Updates ShaderUnit::code in-place. Does NOT bump version — user must
 * press Compile (ShaderCompileRequested) to apply to the GPU pipeline.
 */
struct ShaderCodeEdited
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int (0=VERTEX, 1=FRAGMENT)
	std::string code;         ///< New GLSL source text
};

/**
 * @brief Emitted when the user edits a single field in a ShaderStruct container.
 *
 * Fine grained: identifies the section (which list), the field index (which
 * entry), the field name (which property of that entry), and the new value.
 * For StructDefs, subFieldIndex identifies which member field of the struct
 * definition is being edited (-1 = the struct definition itself: name/varName).
 * The IR is mutated in place; the generated GLSL (Code mode) is only refreshed
 * on the next Compile (Struct path). Does NOT bump version — user must press
 * Compile.
 */
struct ShaderStructEdited
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
	ShaderSection section;    ///< Which ShaderStruct container
	int fieldIndex = 0;       ///< Index into the section's vector
	int subFieldIndex = -1;   ///< For StructDefs: which member field (-1 = struct itself)
	std::string field;        ///< Property name ("type", "name", "value", "body", ...)
	std::string value;        ///< New value as string
};

/**
 * @brief Emitted when the user clicks the "+" add button on a struct section.
 *
 * Appends a new default entry to the specified ShaderStruct container.
 * For StructDefs, subFieldIndex identifies which struct definition to add
 * a member field to (-1 = add a new struct definition).
 * The IR is mutated in place; the generated GLSL (Code mode) is only refreshed
 * on the next Compile (Struct path). Does NOT bump version.
 */
struct ShaderFieldAdded
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
	ShaderSection section;    ///< Which ShaderStruct container to append to
	int subFieldIndex = -1;   ///< For StructDefs: which struct def to add a member to
};

/**
 * @brief Emitted when the user clicks "Compile" to compile GLSL -> SPIR-V.
 *
 * Compiles the specified stage via ShaderLibrary::Compile, stores spv in the
 * ShaderUnit, and bumps Shader::m_version on success. PipelineCache sees
 * the new version and rebuilds the pipeline on the next frame.
 *
 * unitType: 0 = compile from code (re-parse then generate), 1 = compile from struct IR.
 */
struct ShaderCompileRequested
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
	int unitType = 0;         ///< 0 = Code path, 1 = Struct path
};

/**
 * @brief Emitted when a bounded shader code-edit gesture begins (editor focus-in).
 *
 * Mirrors ConfigEditBegin: ShaderController captures the "before" source
 * ({code, parsed IR}) for the (object, stage) on this event and applies live
 * keystrokes without recording, so the whole edit burst collapses to one undo
 * entry committed on ShaderEditEnd.
 */
struct ShaderEditBegin
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
};

/**
 * @brief Emitted when a bounded shader code-edit gesture ends (editor focus-out).
 *
 * ShaderController records a single SetShaderCodeOp spanning the code text
 * captured at ShaderEditBegin through the current code, if it actually changed.
 */
struct ShaderEditEnd
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
};

// ---------------------------------------------------------------------------
// Undo/redo restore events (replayed by the shader operations)
//
// These mirror the forward edit events 1:1 in field detail, but are distinct
// event types so replay can bump the ShaderUnit version (refreshing the panel)
// while forward live edits deliberately do NOT bump (avoiding a cursor-jumping
// reload mid-typing). All are CPU-only: they restore editable source without
// recompiling to SPIR-V.
// ---------------------------------------------------------------------------

/**
 * @brief Restores a stage's GLSL code text (replayed by SetShaderCodeOp).
 *
 * Mirrors ShaderCodeEdited. ShaderController overwrites ShaderUnit::code and
 * bumps the ShaderUnit version so the editor panel refreshes.
 */
struct ShaderCodeRestored
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
	std::string code;         ///< Absolute GLSL text to restore
};

/**
 * @brief Restores one ShaderStruct element (replayed by SetShaderFieldOp).
 *
 * Carries the whole addressed element (a ShaderFieldValue). ShaderController
 * assigns it into the section's vector at fieldIndex, then bumps the version so
 * the editor panel refreshes. Whole-element assignment (rather than replaying a
 * single {field,value}) makes both undo and redo a plain copy.
 */
struct ShaderFieldRestored
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
	ShaderSection section;    ///< Which ShaderStruct container
	int fieldIndex = 0;       ///< Index into the section's vector
	ShaderFieldValue value;   ///< Whole element to assign back
};

/**
 * @brief Re-appends a default entry to a ShaderStruct container (redo of an add).
 *
 * Mirrors ShaderFieldAdded. Replayed by AddShaderFieldOp when re-applying an
 * add. ShaderController appends the same default entry and bumps the version.
 */
struct ShaderFieldAddRestored
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
	ShaderSection section;    ///< Which ShaderStruct container to append to
	int subFieldIndex = -1;   ///< For StructDefs: which struct def to add a member to
};

/**
 * @brief Removes the last entry from a ShaderStruct container (undo of an add).
 *
 * Replayed by AddShaderFieldOp when inverting an add. Because undo/redo is
 * strictly LIFO, the added entry is always the last one, so dropping the last
 * element is safe. ShaderController bumps the version after removal.
 */
struct ShaderFieldRemoved
{
	int objectUid = 0;
	int stage = 0;            ///< ShaderType as int
	ShaderSection section;    ///< Which ShaderStruct container to remove from
	int subFieldIndex = -1;   ///< For StructDefs: which struct def to remove a member from
};

} // namespace neurus