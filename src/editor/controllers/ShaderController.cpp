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

#include <memory>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Helper: resolve ObjectID* -> Mesh* (the only shader-owning object type)
// ---------------------------------------------------------------------------

/**
 * @brief Casts a const ObjectID* to a non-const Mesh*.
 *
 * The ShaderEditorPanel only emits events for GO_MESH objects (checked
 * in Refresh), so this cast is safe. The const_cast is needed because
 * Selections stores const ObjectID* but the underlying objects are
 * mutable (owned by Scene's shared_ptr maps).
 */
neurus::Mesh* AsMesh(const neurus::ObjectID* obj)
{
	if (!obj || obj->o_type != neurus::ObjectID::GOType::GO_MESH)
		return nullptr;
	return static_cast<neurus::Mesh*>(const_cast<neurus::ObjectID*>(obj));
}

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
	auto* mesh = AsMesh(e.object);
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
	auto* mesh = AsMesh(e.object);
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
	auto* mesh = AsMesh(e.object);
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
 * the correct property. After mutation, regenerates GLSL via ShaderGenerator.
 * Does NOT bump version — user must press Compile.
 */
void OnStructEdited(const neurus::ShaderStructEdited& e)
{
	auto* mesh = AsMesh(e.object);
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
	case neurus::ShaderSection::StructDefs:
		if (e.fieldIndex < static_cast<int>(parsed.struct_def_list.size()))
		{
			auto& sd = parsed.struct_def_list[e.fieldIndex];
			if (e.field == "name") { sd.name = e.value; changed = true; }
			else if (e.field == "type") { sd.varName = e.value; changed = true; }
		}
		break;
	case neurus::ShaderSection::Functions:
		if (e.fieldIndex < static_cast<int>(parsed.func_list.size()))
		{
			auto& f = parsed.func_list[e.fieldIndex];
			if (e.field == "name") { f.name = e.value; changed = true; }
			else if (e.field == "type") { f.returnType = neurus::ShaderStruct::ParseType(e.value); changed = true; }
		}
		break;
	}

	if (changed)
	{
		NEURUS_LOG("[ShaderController] Struct edited: section=" << static_cast<int>(e.section)
		           << " idx=" << e.fieldIndex << " field=" << e.field << " value=" << e.value);
	}
}

} // anonymous namespace

namespace neurus {

// ---------------------------------------------------------------------------
// Init — subscribe to shader events
// ---------------------------------------------------------------------------

void ShaderController::Init(EventQueue& bus)
{
	bus.subscribe<ShaderCreateRequested>(
		[](const ShaderCreateRequested& e) {
			OnCreateShader(e);
		});
	bus.subscribe<ShaderCompileRequested>(
		[](const ShaderCompileRequested& e) {
			OnCompileShader(e);
		});
	bus.subscribe<ShaderCodeEdited>(
		[](const ShaderCodeEdited& e) {
			OnCodeEdited(e);
		});
	bus.subscribe<ShaderStructEdited>(
		[](const ShaderStructEdited& e) {
			OnStructEdited(e);
		});
}

} // namespace neurus