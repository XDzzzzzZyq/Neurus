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

#include "UID.h"
#include "Transform.h"

namespace neurus
{

// --- Forward declarations -------------------------------------------------

class Material;
class MeshData;

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

	bool using_shadow   = true;  ///< Enable shadow casting for this mesh
	bool using_material = true;  ///< Enable material shading (if false, use flat shading)
	bool using_sdf      = true;  ///< Include this mesh in SDF field generation
	bool is_closure     = true;  ///< Mesh is closed/watertight (affects SDF and culling)

	/// OBJ file path for serialization and asset recovery
	std::string o_meshPath;

	/**
	 * @brief Constructs a default empty mesh.
	 * @note Mesh has no geometry until o_mesh is assigned.
	 */
	Mesh();

	/**
	 * @brief Constructs a mesh from an OBJ file.
	 * @param path Path to OBJ file on disk.
	 * @note Automatically loads MeshData from file via MeshData::LoadObj().
	 *       If loading fails, o_mesh remains nullptr.
	 */
	explicit Mesh(const std::string& path);

	/**
	 * @brief Virtual destructor.
	 */
	~Mesh() override = default;

	/**
	 * @brief Cereal serialization for mesh.
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 * @note GPU resources (o_material, o_mesh, o_shader) are NOT serialized.
	 *       Only o_meshPath stores the asset reference for recovery.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::base_class<ObjectID>(this),
		   cereal::make_nvp("transform", cereal::base_class<Transform3D>(this)),
		   CEREAL_NVP(o_meshPath),
		   CEREAL_NVP(using_shadow), CEREAL_NVP(using_material),
		   CEREAL_NVP(using_sdf), CEREAL_NVP(is_closure));
	}

	// Non-copyable (UID base enforces this, but clarify for Mesh)
	Mesh(const Mesh&) = delete;
	Mesh& operator=(const Mesh&) = delete;
	Mesh(Mesh&&) = delete;
	Mesh& operator=(Mesh&&) = delete;

	// -------------------------------------------------------------------
	// OBJ reload (for after deserialization)
	// -------------------------------------------------------------------

	/**
	 * @brief Reloads mesh geometry from the stored o_meshPath.
	 *
	 * Cereal deserialization restores o_meshPath but not the loaded MeshData.
	 * Call this after deserialization to reload vertex/index buffers from disk.
	 *
	 * @note No-op if o_meshPath is empty or o_mesh is already loaded.
	 */
	void ReloadMeshData();

	// -------------------------------------------------------------------
	// Shader
	// -------------------------------------------------------------------

	/**
	 * @brief Sets the shader pointer for this mesh.
	 * @param shader Non-owning pointer to the shader to use.
	 */
	void SetObjShader(void* shader);

	// -------------------------------------------------------------------
	// Texture / material parameters
	// -------------------------------------------------------------------

	/**
	 * @brief Assigns texture to a material parameter slot.
	 * @param _type Material parameter type (MAT_ALBEDO, MAT_NORMAL, etc.).
	 * @param _name Texture filename or path (implementation-specific lookup).
	 * @note Safe to call even if o_material is nullptr (becomes no-op).
	 */
	void SetTex(int _type, const std::string& _name);

	/**
	 * @brief Sets a scalar material property.
	 * @param _type Material parameter type (MAT_METAL, MAT_ROUGH, etc.).
	 * @param _val Scalar value (typically 0.0 to 1.0).
	 * @note Safe to call even if o_material is nullptr.
	 */
	void SetMatColor(int _type, float _val);

	/**
	 * @brief Sets a vector material property.
	 * @param _type Material parameter type (MAT_ALBEDO, MAT_EMIS_COL, etc.).
	 * @param _col RGB color vector.
	 * @note Safe to call even if o_material is nullptr.
	 */
	void SetMatColor(int _type, const glm::vec3& _col);

	// -------------------------------------------------------------------
	// Flag toggles
	// -------------------------------------------------------------------

	/**
	 * @brief Enables or disables shadow casting.
	 * @param _enable True to cast shadows, false to skip shadow pass.
	 */
	void EnableShadow(bool _enable) { using_shadow = _enable; }

	/**
	 * @brief Enables or disables material shading.
	 * @param _enable True for full PBR, false for flat/debug shading.
	 */
	void EnableMaterial(bool _enable) { using_material = _enable; }

	/**
	 * @brief Enables or disables SDF contribution.
	 * @param _enable True to include in SDF field, false to exclude.
	 * @note SDF is used for soft shadow approximation.
	 */
	void EnableSDF(bool _enable) { using_sdf = _enable; }

	// -------------------------------------------------------------------
	// Polymorphic accessors (ObjectID overrides)
	// -------------------------------------------------------------------

	/**
	 * @brief Returns pointer to shader.
	 * @return Pointer to shader as void* (cast required).
	 * @note Overrides ObjectID::GetShader() for polymorphic access.
	 */
	void* GetShader() override { return o_shader; }

	/**
	 * @brief Returns pointer to material.
	 * @return Pointer to Material as void* (cast required).
	 * @note Overrides ObjectID::GetMaterial() for polymorphic access.
	 */
	void* GetMaterial() override { return o_material.get(); }

	/**
	 * @brief Returns pointer to this object's Transform component.
	 * @return Pointer to Transform as void* (cast required).
	 * @note Overrides ObjectID::GetTransform() for polymorphic access.
	 *       Returns this as Transform*, leveraging the Transform3D base.
	 */
	void* GetTransform() override { return static_cast<Transform*>(this); }
};

} // namespace neurus
