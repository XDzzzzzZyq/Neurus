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
	Attributes   = 0,  ///< AB_list         (S_IO)        — vertex attributes
	PassOutputs  = 1,  ///< pass_list       (S_IO)        — render-pass outputs
	Inputs       = 2,  ///< input_list      (S_Uniform)   — shader-stage inputs
	Outputs      = 3,  ///< output_list     (S_Uniform)   — shader-stage outputs
	Uniforms     = 4,  ///< uniform_list    (S_Uniform)   — single-value uniforms
	StructDefs   = 5,  ///< struct_def_list (S_StructDef)— bare struct definitions
	Functions    = 6,  ///< func_list       (S_Func)      — user-defined functions
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
 * After mutation, ShaderController regenerates GLSL via ShaderGenerator::Generate.
 * Does NOT bump version — user must press Compile.
 */
struct ShaderStructEdited
{
	const ObjectID* object = nullptr;
	int stage = 0;            ///< ShaderType as int
	ShaderSection section;    ///< Which ShaderStruct container
	int fieldIndex = 0;       ///< Index into the section's vector
	std::string field;        ///< Property name ("type", "name", "value", "body", ...)
	std::string value;        ///< New value as string
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