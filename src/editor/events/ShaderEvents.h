/**
 * @file ShaderEvents.h
 * @brief Event structs for the shader editor pipeline (UI -> Editor -> ShaderController).
 *
 * All events carry a const ObjectID* resolved once by the ShaderEditorPanel
 * from the active scene selection. The ShaderController casts it to Mesh*
 * (in the .cpp only) and operates on the shader data directly.
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

namespace neurus {

class ObjectID;

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
	const ObjectID* object = nullptr;
};

/**
 * @brief Emitted when the user edits the GLSL code text for a stage.
 *
 * Updates ShaderUnit::code in-place. Does NOT bump version — user must
 * press Compile (ShaderCompileRequested) to apply to the GPU pipeline.
 */
struct ShaderCodeEdited
{
	const ObjectID* object = nullptr;
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
	const ObjectID* object = nullptr;
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
	const ObjectID* object = nullptr;
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
	const ObjectID* object = nullptr;
	int stage = 0;            ///< ShaderType as int
	int unitType = 0;         ///< 0 = Code path, 1 = Struct path
};

} // namespace neurus