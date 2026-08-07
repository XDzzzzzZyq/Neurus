#include "scene/Mesh.h"
#include "core/Log.h"
#include "asset/data/MeshData.h"
#include "render/shaders/Shader.h"

namespace neurus
{

Mesh::Mesh()
{
	o_type = ObjectID::GOType::GO_MESH;
	o_name = "Mesh";
}

Mesh::Mesh(const std::string& path)
	: Mesh()
{
	o_meshPath = path;
	if (path.empty()) return;
	auto meshData = std::make_shared<MeshData>();
	if (meshData->LoadObj(path)) o_mesh = meshData;
}

Mesh::~Mesh()
{
}

void Mesh::ReloadMeshData(const std::string& assetDir)
{
	if (!o_meshPath.empty() && !o_mesh)
	{
		const std::string fullPath = assetDir.empty() ? o_meshPath : assetDir + "/" + o_meshPath;
		auto meshData = std::make_shared<MeshData>();
		if (meshData->LoadObj(fullPath)) o_mesh = meshData;
	}
}

void Mesh::SetObjShader(std::shared_ptr<Shader> shader) { o_shader = std::move(shader); }

void* Mesh::GetShaderUnit(int shaderType) const
{
	if (!o_shader) return nullptr;
	auto type = static_cast<ShaderType>(shaderType);
	if (!o_shader->HasStage(type)) return nullptr;
	return const_cast<ShaderUnit*>(&o_shader->GetStage(type));
}

} // namespace neurus