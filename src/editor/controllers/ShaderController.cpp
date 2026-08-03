/**
 * @file ShaderController.cpp
 * @brief Event-driven shader lifecycle: create, compile, code edit, struct edit.
 *
 * Stateless — all handlers are free functions in an anonymous namespace.
 * Each handler receives a discrete shader event, extracts the ObjectID*
 * pointer, casts it to Mesh* (the only object type that owns a shader),
 * and applies the corresponding mutation to the Shader/ShaderUnit data.
 *
 * No Editor*, no DeferredRenderer*, no UploadManager* — pure shader logic.
 * The ObjectID* is provided by each event (resolved once by the
 * ShaderEditorPanel from the active scene selection).
 */

#include "editor/controllers/ShaderController.h"
#include "editor/events/ShaderEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/EventBus.h"

#include "scene/Mesh.h"
#include "scene/UID.h"

#include "render/shaders/Shader.h"
#include "render/shaders/ShaderUnit.h"
#include "render/shaders/ShaderLibrary.h"
#include "render/shaders/ShaderGenerator.h"
#include "render/shaders/ShaderParser.h"
#include "render/shaders/RenderShader.h"

#include "core/Log.h"

#include <algorithm>
#include <memory>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

/**
 * @brief Handles ShaderCreateRequested — loads default gbuffer shaders for a mesh.
 *
 * Flow:
 *   1. LoadRenderShader (gbuffer.vert + gbuffer.frag)
 *   2. ParseAndGenerate (parse -> generate GLSL for both stages)
 *   3. Assign to mesh->o_shader
 *   4. Compile both stages to SPIR-V
 *   5. Bump Shader::m_version on success
 */
void OnCreateShader(const neurus::ShaderCreateRequested& e)
{
	auto* mesh = neurus::Mesh::As(e.object);
	if (!mesh)
	{
		NEURUS_ERR("[ShaderController] OnCreateShader: not a mesh");
		return;
	}

	if (mesh->o_shader)
	{
		NEURUS_LOG("[ShaderController] Mesh already has a shader");
		return;
	}

	const int objectId = mesh->GetObjectID();
	const std::string shaderName = "MeshShader_" + std::to_string(objectId);
	const std::string vertPath = "res/shaders/render/gbuffer.vert";
	const std::string fragPath = "res/shaders/render/gbuffer.frag";

	try
	{
		auto shader = neurus::ShaderLibrary::LoadRenderShader(shaderName, vertPath, fragPath);
		if (!shader || !shader->ParseAndGenerate())
		{
			NEURUS_ERR("[ShaderController] Failed to load/parse default shader for mesh " << objectId);
			return;
		}

		mesh->o_shader = std::move(shader);

		// Compile both stages to SPIR-V and bump version on success
		auto& s = *mesh->o_shader;
		bool allOk = true;

		if (s.HasStage(neurus::ShaderType::VERTEX))
		{
			auto& unit = s.GetStage(neurus::ShaderType::VERTEX);
			unit.spv = neurus::ShaderLibrary::Compile(unit, neurus::ShaderType::VERTEX, s.GetName());
			if (unit.spv.empty()) { allOk = false; }
			else { unit.BumpVersion(); }
		}
		if (s.HasStage(neurus::ShaderType::FRAGMENT))
		{
			auto& unit = s.GetStage(neurus::ShaderType::FRAGMENT);
			unit.spv = neurus::ShaderLibrary::Compile(unit, neurus::ShaderType::FRAGMENT, s.GetName());
			if (unit.spv.empty()) { allOk = false; }
			else { unit.BumpVersion(); }
		}

		if (allOk)
			s.BumpVersion();

		NEURUS_LOG("[ShaderController] Created shader for mesh " << objectId << ": " << shaderName);
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Exception creating shader: " << ex.what());
	}
}

/**
 * @brief Handles ShaderCodeEdited — updates ShaderUnit::code in-place.
 *
 * Does NOT bump version — the user must press Compile (ShaderCompileRequested)
 * to apply the code change to the GPU pipeline.
 */
void OnCodeEdited(const neurus::ShaderCodeEdited& e)
{
	auto* mesh = neurus::Mesh::As(e.object);
	if (!mesh || !mesh->o_shader) return;

	auto& shader = *mesh->o_shader;
	auto type = static_cast<neurus::ShaderType>(e.stage);

	if (!shader.HasStage(type)) return;

	auto& unit = shader.GetStage(type);
	if (unit.code == e.code) return;  // dirty-check

	unit.code = e.code;

	NEURUS_LOG("[ShaderController] Code updated for mesh " << mesh->GetObjectID()
	           << " (" << unit.code.size() << " bytes)");
}

/**
 * @brief Handles ShaderCompileRequested — compiles GLSL -> SPIR-V for a stage.
 *
 * Flow:
 *   unitType == 0 (Code path): re-parse code into IR, regenerate GLSL, compile
 *   unitType == 1 (Struct path): regenerate GLSL from IR, compile
 *
 * Bumps ShaderUnit::m_version and Shader::m_version on success.
 */
void OnCompileShader(const neurus::ShaderCompileRequested& e)
{
	auto* mesh = neurus::Mesh::As(e.object);
	if (!mesh || !mesh->o_shader)
	{
		NEURUS_ERR("[ShaderController] OnCompileShader: no shader on mesh");
		return;
	}

	auto& shader = *mesh->o_shader;
	auto type = static_cast<neurus::ShaderType>(e.stage);

	if (!shader.HasStage(type)) return;
	auto& unit = shader.GetStage(type);

	try
	{
		if (e.unitType == 0)
		{
			// Code path: parse the code into ShaderStruct IR, then generate GLSL
			unit.parsed = neurus::ShaderParser::ParseShaderCode(unit.code, type);
			unit.code = neurus::ShaderGenerator::Generate(unit.parsed);
		}
		else
		{
			// Struct path: generate GLSL from ShaderStruct IR
			unit.code = neurus::ShaderGenerator::Generate(unit.parsed);
		}

		// Compile to SPIR-V and store in ShaderUnit
		unit.spv = neurus::ShaderLibrary::Compile(unit, type, shader.GetName());
		if (unit.spv.empty())
		{
			NEURUS_ERR("[ShaderController] Compile failed for mesh " << mesh->GetObjectID());
			return;
		}
		unit.BumpVersion();
		shader.BumpVersion();

		NEURUS_LOG("[ShaderController] Compiled shader for mesh " << mesh->GetObjectID()
		           << " (unitType=" << e.unitType << ")");
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Compile failed: " << ex.what());
	}
}

/**
 * @brief Handles ShaderStructEdited — mutates a single field in a ShaderStruct container.
 *
 * Dispatches on ShaderSection to the correct vector, then on field name to
 * the correct property. The IR is mutated in place; the generated GLSL (Code
 * mode) is only refreshed on the next Compile (Struct path). Does NOT bump
 * version — user must press Compile.
 */
void OnStructEdited(const neurus::ShaderStructEdited& e)
{
	auto* mesh = neurus::Mesh::As(e.object);
	if (!mesh || !mesh->o_shader) return;

	auto& shader = *mesh->o_shader;
	auto type = static_cast<neurus::ShaderType>(e.stage);

	if (!shader.HasStage(type)) return;
	auto& parsed = shader.GetStage(type).parsed;

	bool changed = false;

	auto applyToIO = [&](std::vector<neurus::S_IO>& list) {
		if (e.fieldIndex >= static_cast<int>(list.size())) return;
		auto& io = list[e.fieldIndex];
		if (e.field == "type")
		{
			io.type = neurus::ShaderStruct::ParseType(e.value);
			io.typeName = e.value;
			changed = true;
		}
		else if (e.field == "name")
		{
			io.name = e.value;
			changed = true;
		}
	};

	auto applyToUniform = [&](std::vector<neurus::S_Uniform>& list) {
		if (e.fieldIndex >= static_cast<int>(list.size())) return;
		auto& u = list[e.fieldIndex];
		if (e.field == "type")
		{
			u.type = neurus::ShaderStruct::ParseType(e.value);
			u.actualType = e.value;
			changed = true;
		}
		else if (e.field == "name")
		{
			u.name = e.value;
			changed = true;
		}
	};

	switch (e.section)
	{
	case neurus::ShaderSection::Attributes:   applyToIO(parsed.AB_list); break;
	case neurus::ShaderSection::PassOutputs:  applyToIO(parsed.pass_list); break;
	case neurus::ShaderSection::Inputs:       applyToUniform(parsed.input_list); break;
	case neurus::ShaderSection::Outputs:      applyToUniform(parsed.output_list); break;
	case neurus::ShaderSection::Uniforms:     applyToUniform(parsed.uniform_list); break;
	case neurus::ShaderSection::Functions:
		if (e.fieldIndex < static_cast<int>(parsed.func_list.size()))
		{
			auto& f = parsed.func_list[e.fieldIndex];
			if (e.field == "name") { f.name = e.value; changed = true; }
			else if (e.field == "type") { f.returnType = neurus::ShaderStruct::ParseType(e.value); changed = true; }
		}
		break;
	case neurus::ShaderSection::PushConstants:
		if (e.fieldIndex < static_cast<int>(parsed.push_constants.size()))
		{
			auto& pc = parsed.push_constants[e.fieldIndex];
			if (e.field == "name") { pc.name = e.value; changed = true; }
			else if (e.field == "type") { pc.typeName = e.value; changed = true; }
		}
		break;
	case neurus::ShaderSection::StructDefs:
		if (e.fieldIndex < static_cast<int>(parsed.struct_def_list.size()))
		{
			auto& sd = parsed.struct_def_list[e.fieldIndex];
			if (e.subFieldIndex < 0)
			{
				// Editing the struct definition itself (name or varName)
				if (e.field == "name") { sd.name = e.value; changed = true; }
				else if (e.field == "type") { sd.varName = e.value; changed = true; }
			}
			else if (e.subFieldIndex < static_cast<int>(sd.fields.size()))
			{
				// Editing a member field of the struct definition
				auto& io = sd.fields[e.subFieldIndex];
				if (e.field == "type")
				{
					io.type = neurus::ShaderStruct::ParseType(e.value);
					io.typeName = e.value;
					changed = true;
				}
				else if (e.field == "name")
				{
					io.name = e.value;
					changed = true;
				}
			}
		}
		break;
	}

	if (changed)
	{
		NEURUS_LOG("[ShaderController] Struct edited: section=" << static_cast<int>(e.section)
		           << " idx=" << e.fieldIndex << " field=" << e.field << " value=" << e.value);
	}
}

/**
 * @brief Generates a unique name by appending a counter if the base name exists.
 *
 * Scans the provided list of existing names and appends "_2", "_3", etc.
 * until a unique name is found. Used by OnFieldAdded to avoid duplicate
 * field/struct names within the same container.
 */
std::string UniqueName(const std::string& base, const std::vector<std::string>& existing)
{
	auto exists = [&](const std::string& n) {
		return std::find(existing.begin(), existing.end(), n) != existing.end();
	};
	if (!exists(base)) return base;
	for (int i = 2; ; ++i)
	{
		std::string candidate = base + "_" + std::to_string(i);
		if (!exists(candidate)) return candidate;
	}
}

/**
 * @brief Handles ShaderFieldAdded — appends a new default entry to a ShaderStruct container.
 *
 * Dispatches on ShaderSection to append a default-constructed entry to the
 * correct vector. For StructDefs with subFieldIndex >= 0, appends a member
 * field to the specified struct definition. The IR is mutated in place; the
 * generated GLSL (Code mode) is only refreshed on the next Compile (Struct
 * path). Does NOT bump version — user must press Compile.
 */
void OnFieldAdded(const neurus::ShaderFieldAdded& e)
{
	auto* mesh = neurus::Mesh::As(e.object);
	if (!mesh || !mesh->o_shader) return;

	auto& shader = *mesh->o_shader;
	auto type = static_cast<neurus::ShaderType>(e.stage);

	if (!shader.HasStage(type)) return;
	auto& parsed = shader.GetStage(type).parsed;

	switch (e.section)
	{
	case neurus::ShaderSection::Attributes:
	{
		std::vector<std::string> names;
		for (const auto& io : parsed.AB_list) names.push_back(io.name);
		parsed.AB_list.push_back({static_cast<int>(parsed.AB_list.size()),
		                          UniqueName("new_attr", names), neurus::ParaType::FLOAT, "", neurus::Interp::Smooth});
		break;
	}
	case neurus::ShaderSection::PassOutputs:
	{
		std::vector<std::string> names;
		for (const auto& io : parsed.pass_list) names.push_back(io.name);
		parsed.pass_list.push_back({static_cast<int>(parsed.pass_list.size()),
		                            UniqueName("new_output", names), neurus::ParaType::FLOAT, "", neurus::Interp::Smooth});
		break;
	}
	case neurus::ShaderSection::Inputs:
	{
		std::vector<std::string> names;
		for (const auto& u : parsed.input_list) names.push_back(u.name);
		parsed.input_list.push_back({UniqueName("new_input", names), neurus::ParaType::FLOAT, 1, -1, "", ""});
		break;
	}
	case neurus::ShaderSection::Outputs:
	{
		std::vector<std::string> names;
		for (const auto& u : parsed.output_list) names.push_back(u.name);
		parsed.output_list.push_back({UniqueName("new_output", names), neurus::ParaType::FLOAT, 1, -1, "", ""});
		break;
	}
	case neurus::ShaderSection::Uniforms:
	{
		std::vector<std::string> names;
		for (const auto& u : parsed.uniform_list) names.push_back(u.name);
		parsed.uniform_list.push_back({UniqueName("new_uniform", names), neurus::ParaType::FLOAT, 1, -1, "", ""});
		break;
	}
	case neurus::ShaderSection::Functions:
	{
		std::vector<std::string> names;
		for (const auto& f : parsed.func_list) names.push_back(f.name);
		parsed.func_list.push_back({neurus::ParaType::FLOAT, UniqueName("new_func", names), "", {}});
		break;
	}
	case neurus::ShaderSection::PushConstants:
	{
		std::vector<std::string> names;
		for (const auto& pc : parsed.push_constants) names.push_back(pc.name);
		parsed.push_constants.push_back({UniqueName("new_pc", names), 0, 0, "float"});
		break;
	}
	case neurus::ShaderSection::StructDefs:
		if (e.subFieldIndex < 0)
		{
			// Add a new struct definition
			std::vector<std::string> names;
			for (const auto& sd : parsed.struct_def_list) names.push_back(sd.name);
			parsed.struct_def_list.push_back({0, UniqueName("NewStruct", names), {}, ""});
		}
		else if (e.subFieldIndex < static_cast<int>(parsed.struct_def_list.size()))
		{
			// Add a member field to the specified struct definition
			auto& fields = parsed.struct_def_list[e.subFieldIndex].fields;
			std::vector<std::string> names;
			for (const auto& mf : fields) names.push_back(mf.name);
			fields.push_back({0, UniqueName("new_field", names), neurus::ParaType::FLOAT, "", neurus::Interp::Smooth});
		}
		break;
	}

	NEURUS_LOG("[ShaderController] Field added: section=" << static_cast<int>(e.section)
	           << " subIdx=" << e.subFieldIndex);
}

} // anonymous namespace

namespace neurus {

// ---------------------------------------------------------------------------
// Init — subscribe to shader events
// ---------------------------------------------------------------------------

void ShaderController::Init(EventQueue& bus, IOperationSink& /*ops*/)
{
	// Create/Compile bump the shader version -> pipeline rebuilds on the next
	// frame, so temporal accumulation (shadow intensity) must reset. Code/struct
	// edits do NOT bump the version and only apply on the next Compile.
	bus.subscribe<ShaderCreateRequested>(
		[&bus](const ShaderCreateRequested& e) {
			OnCreateShader(e);
			bus.enqueue(RenderResetEvent{});
		});
	bus.subscribe<ShaderCompileRequested>(
		[&bus](const ShaderCompileRequested& e) {
			OnCompileShader(e);
			bus.enqueue(RenderResetEvent{});
		});
	bus.subscribe<ShaderCodeEdited>(
		[](const ShaderCodeEdited& e) {
			OnCodeEdited(e);
		});
	bus.subscribe<ShaderStructEdited>(
		[](const ShaderStructEdited& e) {
			OnStructEdited(e);
		});
	bus.subscribe<ShaderFieldAdded>(
		[](const ShaderFieldAdded& e) {
			OnFieldAdded(e);
		});
}

} // namespace neurus