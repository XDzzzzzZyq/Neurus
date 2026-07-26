/**
 * @file ShaderController.cpp
 * @brief Shader lifecycle controller — create and compile per-mesh shaders.
 *
 * OnCreateShader: loads the default gbuffer vertex/fragment shaders as a
 * template, parses them into ShaderUnits, and attaches to the mesh.
 *
 * OnCompileShader: compiles the attached shader's GLSL to SPIR-V and marks
 * the scene as ShaderChanged so the renderer rebuilds pipelines on the next
 * frame. Actual pipeline upload is deferred to the renderer (UploadManager).
 */

#include "ShaderController.h"

#include "editor/Editor.h"
#include "editor/events/EventBus.h"
#include "editor/events/ShaderEvents.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "render/shaders/Shader.h"
#include "render/shaders/ShaderLibrary.h"
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

		// Mark scene as changed so renderer knows to rebuild pipelines
		scene.UpdateSceneStatus(Scene::ShaderChanged, true);

		NEURUS_LOG("[ShaderController] Created shader for mesh " << e.objectId << ": " << shaderName);
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Exception creating shader: " << ex.what());
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

		// Compile all stages to SPIR-V
		auto spvMap = ShaderLibrary::CompileAll(shader);

		// The compiled SPIR-V is now stored in the shader units.
		// Mark scene as ShaderChanged so the renderer picks it up on the
		// next frame and builds pipelines via UploadManager::UploadShader().
		scene.UpdateSceneStatus(Scene::ShaderChanged, true);

		NEURUS_LOG("[ShaderController] Compiled shader for mesh " << e.objectId
		           << " (" << spvMap.size() << " stages)");
	}
	catch (const std::exception& ex)
	{
		NEURUS_ERR("[ShaderController] Compile failed: " << ex.what());
	}
}

} // namespace neurus
