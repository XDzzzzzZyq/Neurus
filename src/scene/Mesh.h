/**
 * @file Mesh.h
 * @brief 3D mesh object for renderable geometry in the scene hierarchy.
 *
 * Mesh is the primary renderable object, representing 3D geometry with material,
 * transform, and shader properties. It inherits from ObjectID (scene identity) and
 * Transform3D (spatial placement). Mesh objects are consumed by the Renderer to
 * produce final images.
 *
 * Architecture:
 * - Owned by scene graph (shared_ptr in Scene containers)
 * - Renderer reads mesh data, material, and transform as immutable
 * - Editor mutates mesh properties via events and controllers
 * - LOD support deferred (post-MVP)
 *
 * @note GPU resources (buffers, textures) are owned by MeshData and Material, not Mesh.
 * @note Multiple meshes can share the same MeshData and Material via shared_ptr.
 */

#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include <cereal/types/base_class.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "UID.h"
#include "Transform.h"

namespace neurus
{

// --- Forward declarations -------------------------------------------------

class Material;
class MeshData;
class VertexBuffer;
class IndexBuffer;

/**
 * @brief 3D mesh object representing renderable geometry with material and transform.
 *
 * Mesh combines MeshData (vertex/index buffers), Material (textures and properties),
 * void* shader (GPU program), and Transform3D (position/rotation/scale) to form a
 * complete renderable object. It supports shadow casting, material shading, and SDF
 * (Signed Distance Field) generation for soft shadows.
 *
 * Resource Ownership:
 * - MeshData: Shared ownership (multiple meshes can reference same geometry)
 * - Material: Shared ownership (multiple meshes can share materials)
 * - Shader:   Void pointer (owner is external; Mesh does not manage lifetime)
 * - Transform: Owned directly (each mesh has unique transform via Transform3D)
 *
 * @note Inheritance: ObjectID for scene identity, Transform3D for spatial transform.
 * @note Thread-safety: Not thread-safe. Access from main thread only.
 */
class Mesh : public ObjectID, public Transform3D
{
public:
	/// Material defining surface properties (albedo, metallic, roughness, etc.)
	std::shared_ptr<Material> o_material;

	/// High-resolution geometry (vertex/index buffers)
	std::shared_ptr<MeshData> o_mesh;

	/// Shader program for rendering this mesh (non-owning void pointer)
	void* o_shader = nullptr;

	bool using_shadow   = true;
	bool using_material = true;
	bool using_sdf      = true;
	bool is_closure     = true;

	/// OBJ file path for serialization and asset recovery
	std::string o_meshPath;

	Mesh();
	explicit Mesh(const std::string& path);
	~Mesh() override;

	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::base_class<ObjectID>(this),
		   cereal::make_nvp("transform", cereal::base_class<Transform3D>(this)),
		   CEREAL_NVP(o_meshPath),
		   CEREAL_NVP(using_shadow), CEREAL_NVP(using_material),
		   CEREAL_NVP(using_sdf), CEREAL_NVP(is_closure));
	}

	Mesh(const Mesh&) = delete;
	Mesh& operator=(const Mesh&) = delete;
	Mesh(Mesh&&) = delete;
	Mesh& operator=(Mesh&&) = delete;

	void ReloadMeshData(const std::string& assetDir = "");

	void UploadToGPU(const vk::raii::Device& device,
	                 const vk::raii::PhysicalDevice& physicalDevice,
	                 vk::Queue queue,
	                 uint32_t queueFamilyIndex);

	const VertexBuffer* GetVertexBuffer() const { return m_gpuVertices.get(); }
	const IndexBuffer* GetIndexBuffer() const { return m_gpuIndices.get(); }
	uint32_t GetGPUIndexCount() const { return m_gpuIndexCount; }
	void ReleaseGPUBuffers();

	void SetObjShader(void* shader);
	void SetTex(int _type, const std::string& _name);
	void SetMatColor(int _type, float _val);
	void SetMatColor(int _type, const glm::vec3& _col);
	void EnableShadow(bool _enable) { using_shadow = _enable; }
	void EnableMaterial(bool _enable) { using_material = _enable; }
	void EnableSDF(bool _enable) { using_sdf = _enable; }
	void* GetShader() override { return o_shader; }
	void* GetMaterial() override { return o_material.get(); }
	void* GetTransform() override { return static_cast<Transform*>(this); }

private:
	std::unique_ptr<VertexBuffer> m_gpuVertices;
	std::unique_ptr<IndexBuffer> m_gpuIndices;
	uint32_t m_gpuIndexCount = 0;
};

} // namespace neurus