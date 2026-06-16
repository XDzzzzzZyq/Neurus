/**
 * @file Mesh.cpp
 * @brief Implementation of Mesh scene object.
 */

#include "scene/Mesh.h"

#include "data/MeshData.h"
#include "render/Material.h"

namespace neurus
{

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

Mesh::Mesh()
{
	o_type = ObjectID::GOType::GO_MESH;
	o_name = "Mesh";
}

Mesh::Mesh(const std::string& path)
	: Mesh()
{
	if (path.empty())
	{
		return;
	}

	auto meshData = std::make_shared<MeshData>();
	if (meshData->LoadObj(path))
	{
		o_mesh = meshData;
	}
}

// -----------------------------------------------------------------------
// Shader
// -----------------------------------------------------------------------

void Mesh::SetObjShader(void* shader)
{
	o_shader = shader;
}

// -----------------------------------------------------------------------
// Texture / material parameters
// -----------------------------------------------------------------------

void Mesh::SetTex(int /*_type*/, const std::string& /*_name*/)
{
	// Stub for MVP.
	// Full implementation requires Vulkan device/queue to load textures
	// via TextureLib, which is not available in the scene layer.
	// This will be implemented when the texture pipeline is integrated
	// with the Editor/Controller layer.
}

void Mesh::SetMatColor(int _type, float _val)
{
	if (!o_material)
	{
		return;
	}
	o_material->SetMatParam(static_cast<Material::MatParaType>(_type), _val);
}

void Mesh::SetMatColor(int _type, const glm::vec3& _col)
{
	if (!o_material)
	{
		return;
	}
	o_material->SetMatParam(static_cast<Material::MatParaType>(_type), _col);
}

} // namespace neurus
