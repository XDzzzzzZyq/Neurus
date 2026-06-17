/**
 * @file Mesh.cpp
 * @brief Implementation of Mesh scene object.
 */

#include "scene/Mesh.h"

#include "data/MeshData.h"

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

// Texture / material parameters are implemented in src/render/MeshBindings.cpp
// to maintain layer isolation (scene must not depend on render).

} // namespace neurus
