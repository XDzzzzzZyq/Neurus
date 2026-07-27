/**
 * @file ShaderController.cpp
 * @brief Shader lifecycle controller — create, edit, compile per-mesh shaders.
 */

#include "ShaderController.h"

#include "editor/Editor.h"
#include "editor/events/EventBus.h"
#include "editor/events/ShaderEvents.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "render/shaders/Shader.h"
#include "render/shaders/ShaderLibrary.h"
#include "render/shaders/ShaderGenerator.h"
#include "render/shaders/ShaderParser.h"
#include "render/shaders/RenderShader.h"
#include "core/Log.h"

#include <memory>
#include <string>

namespace neurus
{

ShaderController::ShaderController(Editor* editor, DeferredRenderer* renderer, UploadManager* uploadManager)
	: c_editor(editor)
	, c_renderer(renderer)
	, c_uploadManager(uploadManager)
{
}

void ShaderController::Init(EventQueue& bus)
{
	bus.subscribe<ShaderCreateRequested>(
		[this](const ShaderCreateRequested& e) { OnCreateShader(e); });
	bus.subscribe<ShaderCompileRequested>(
		[this](const ShaderCompileRequested& e) { OnCompileShader(e); });
	bus.subscribe<ShaderCodeEdited>(
		[this](const ShaderCodeEdited& e) { OnCodeEdited(e); });
	bus.subscribe<ShaderModified>(
		[this](const ShaderModified& e) { OnStructModified(e); });
}

void ShaderController::OnCreateShader(const ShaderCreateRequested& e)
{
	auto& scene = c_editor->GetScene();
	auto it = scene.mesh_list.find(e.objectId);
	if (it == scene.mesh_list.end())
	{
		NEURUS_ERR("[ShaderController] Mesh not found for objectId=" << e.objectId);
		return;
	}

	auto& mesh = it->second;
	if (mesh->o_shader)
	{
		NEURUS_LOG("[ShaderController] Mesh " << e.objectId << " already has a shader");
		return;
	}

	const std::string shaderName = "MeshShader_" + std::to_string(e.objectId);
	const std::string vertPath = "res/shaders/render/gbuffer.vert";
	const std::string fragPath = "res/shaders/render/gbuffer.frag";

	try
	{
		auto shader = ShaderLibrary::LoadRenderShader(shaderName, vertPath, fragPath);
		if (!shader || !shader->ParseAndGenerate())
		{
			NEURUS_ERR("[ShaderController] Failed to load/parse default shader for mesh " << e.objectId);
			return;
		}

		mesh->o_shader = std::move(shader);

		scene.UpdateSceneStatus(Scene::ShaderChanged, true);

		// Parse and generate each stage (CPU only — pipeline creation is handled by GeometryPass)
		if (mesh->o_shader->HasStage(ShaderType::VERTEX))
			OnCompileShader({e.objectId, static_cast<int>(ShaderType::VERTEX), 0});
		if (mesh->o_shader->HasStage(ShaderType::FRAGMENT))
			OnCompileShader({e.objectId, static_cast<int>(ShaderType::FRAGMENT), 0});

		NEURUS_LOG("[ShaderController] Created shader for mesh " << e.objectId << ": " << shaderName);
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Exception creating shader: " << ex.what());
	}
}

void ShaderController::OnCodeEdited(const ShaderCodeEdited& e)
{
	auto& scene = c_editor->GetScene();
	auto it = scene.mesh_list.find(e.objectId);
	if (it == scene.mesh_list.end()) return;

	auto& mesh = it->second;
	if (!mesh->o_shader) return;

	try
	{
		auto& shader = *mesh->o_shader;
		auto type = static_cast<ShaderType>(e.shaderType);

		if (!shader.HasStage(type)) return;

		auto& unit = shader.GetStage(type);
		if (unit.code == e.code) return;  // dirty-check

		unit.code = e.code;
		unit.BumpVersion();

		NEURUS_LOG("[ShaderController] Code updated for mesh " << e.objectId
		           << " (" << unit.code.size() << " bytes)");
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Code update failed: " << ex.what());
	}
}

void ShaderController::OnCompileShader(const ShaderCompileRequested& e)
{
	auto& scene = c_editor->GetScene();
	auto it = scene.mesh_list.find(e.objectId);
	if (it == scene.mesh_list.end())
	{
		NEURUS_ERR("[ShaderController] Mesh not found for objectId=" << e.objectId);
		return;
	}

	auto& mesh = it->second;
	if (!mesh->o_shader)
	{
		NEURUS_ERR("[ShaderController] No shader on mesh " << e.objectId);
		return;
	}

	try
	{
		auto& shader = *mesh->o_shader;
		auto type = static_cast<ShaderType>(e.shaderType);

		if (!shader.HasStage(type)) return;
		auto& unit = shader.GetStage(type);

		if (e.unitType == 0)
		{
			// Code: parse the code into ShaderStruct IR, then generate GLSL
			unit.parsed = ShaderParser::ParseShaderCode(unit.code, type);
			unit.code = ShaderGenerator::Generate(unit.parsed);
		}
		else
		{
			// Struct: generate GLSL from ShaderStruct IR
			unit.code = ShaderGenerator::Generate(unit.parsed);
		}

		unit.BumpVersion();
		scene.UpdateSceneStatus(Scene::ShaderChanged, true);

		NEURUS_LOG("[ShaderController] Compiled shader for mesh " << e.objectId
		           << " (unitType=" << e.unitType << ")");
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Compile failed: " << ex.what());
	}
}

void ShaderController::OnStructModified(const ShaderModified& e)
{
	auto& scene = c_editor->GetScene();
	auto it = scene.mesh_list.find(e.objectId);
	if (it == scene.mesh_list.end()) return;

	auto& mesh = it->second;
	if (!mesh->o_shader) return;

	try
	{
		auto& shader = *mesh->o_shader;
		auto type = static_cast<ShaderType>(e.shaderType);

		if (!shader.HasStage(type)) return;
		auto& parsed = shader.GetStage(type).parsed;

		// Apply the field edit to the correct ShaderStruct list
		bool changed = false;

		auto applyToIO = [&](std::vector<S_IO>& list) {
			if (e.fieldIndex >= static_cast<int>(list.size())) return;
			auto& io = list[e.fieldIndex];
			if (e.field == "type")
			{
				io.type = ShaderStruct::ParseType(e.value);
				io.typeName = e.value;
				changed = true;
			}
			else if (e.field == "name")
			{
				io.name = e.value;
				changed = true;
			}
		};

		auto applyToUniform = [&](std::vector<S_Uniform>& list) {
			if (e.fieldIndex >= static_cast<int>(list.size())) return;
			auto& u = list[e.fieldIndex];
			if (e.field == "type")
			{
				u.type = ShaderStruct::ParseType(e.value);
				u.actualType = e.value;
				u.actualType = e.value;
				changed = true;
			}
			else if (e.field == "name")
			{
				u.name = e.value;
				changed = true;
			}
		};

		switch (e.sectionType)
		{
		case static_cast<int>(Component::Attributes):  applyToIO(parsed.AB_list); break;
		case static_cast<int>(Component::PassOutputs): applyToIO(parsed.pass_list); break;
		case static_cast<int>(Component::Inputs):      applyToUniform(parsed.input_list); break;
		case static_cast<int>(Component::Outputs):     applyToUniform(parsed.output_list); break;
		case static_cast<int>(Component::Uniforms):    applyToUniform(parsed.uniform_list); break;
		case static_cast<int>(Component::StructDefs):  // struct_def_list
			if (e.fieldIndex < static_cast<int>(parsed.struct_def_list.size()))
			{
				auto& sd = parsed.struct_def_list[e.fieldIndex];
				if (e.field == "name") { sd.name = e.value; changed = true; }
				else if (e.field == "type") { sd.varName = e.value; changed = true; }
			}
			break;
		case static_cast<int>(Component::Functions):  // func_list
			if (e.fieldIndex < static_cast<int>(parsed.func_list.size()))
			{
				auto& f = parsed.func_list[e.fieldIndex];
				if (e.field == "name") { f.name = e.value; changed = true; }
				else if (e.field == "type") { f.returnType = ShaderStruct::ParseType(e.value); changed = true; }
			}
			break;
		default: return;
		}

		if (changed)
		{
			auto& unit = shader.GetStage(type);
			unit.BumpVersion();
			scene.UpdateSceneStatus(Scene::ShaderChanged, true);
			NEURUS_LOG("[ShaderController] Struct modified: section=" << e.sectionType
			           << " idx=" << e.fieldIndex << " field=" << e.field << " value=" << e.value);
		}
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Struct modify failed: " << ex.what());
	}
}

} // namespace neurus
