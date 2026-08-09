/**
 * @file ObjectID.h
 * @brief Scene identity + metadata base class for all scene graph objects.
 *
 * ObjectID extends the core UID primitive with scene-specific metadata:
 * - Object name for display in editor UI
 * - Type enumeration for runtime type discrimination
 * - Visibility flags for viewport and rendering
 * - Polymorphic accessors for optional components (Transform, Shader, Material)
 *
 * All scene objects (Camera, Light, Mesh, ...) inherit from ObjectID to
 * participate in the scene graph.
 *
 * Architecture:
 * - UID (core/UID.h) provides globally unique integer IDs
 * - ObjectID extends UID with scene-specific metadata
 * - Scene containers hold ObjectID-derived objects as shared_ptr
 * - ID-based lookups enable efficient object management
 */

#pragma once

#include <string>

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>

#include "core/UID.h"

namespace neurus
{

/**
 * @brief Base class for all scene graph objects with type and component access.
 *
 * ObjectID extends UID with scene-specific functionality:
 * - Object name for display in editor UI
 * - Type enumeration for runtime type discrimination
 * - Visibility flags for viewport and rendering
 * - Polymorphic accessors for optional components (Transform, Shader, Material)
 *
 * All scene objects inherit from ObjectID to participate in the scene graph.
 * Type-specific data is stored in derived classes (Camera, Light, Mesh, etc.).
 */
class ObjectID : public UID
{
public:
	/**
	 * @brief Enumeration of scene object types.
	 *
	 * Used for runtime type identification without RTTI. Enables type-specific
	 * rendering paths and UI display.
	 */
	enum class GOType
	{
		NONE_GO = -1,   ///< Invalid or uninitialized object
		GO_CAM,         ///< Camera object
		GO_MESH,        ///< Mesh geometry
		GO_LIGHT,       ///< Light source (point, sun, spot, area)
		GO_POLYLIGHT,   ///< Polygonal area light
		GO_ENVIR,       ///< Environment map for IBL
		GO_SPRITE,      ///< 2D sprite
		GO_DL,          ///< Debug line primitive
		GO_DP,          ///< Debug point primitive
		GO_DM,          ///< Debug mesh
		GO_SDFFIELD     ///< SDF volume for soft shadows
	};

public:
	std::string o_name;                        ///< Display name in editor UI
	GOType o_type = GOType::NONE_GO;           ///< Runtime type identifier

	mutable bool is_viewport = true;           ///< Visible in viewport (editor-only)
	mutable bool is_rendered = true;           ///< Included in rendering pipeline

	/**
	 * @brief Sets visibility flags for viewport and rendering.
	 * @param v Viewport visibility (editor display).
	 * @param r Render visibility (included in render passes).
	 */
	void SetVisible(bool v, bool r)
	{
		is_viewport = v;
		is_rendered = r;
	}

	/**
	 * @brief Casts a const UID* back to ObjectID* (no type check - the caller
	 *        knows the event's object payload is an ObjectID-derived object).
	 * @param uid Base UID pointer from an event payload.
	 * @return Non-owning ObjectID*, or nullptr if null.
	 */
	static ObjectID* As(const UID* uid)
	{
		if (!uid) return nullptr;
		return static_cast<ObjectID*>(const_cast<UID*>(uid));
	}

	/**
	 * @brief Typed cast: casts a const UID* to T* iff o_type matches T::Type.
	 * @tparam T ObjectID-derived type declaring `static constexpr ObjectID::GOType Type`.
	 * @param uid Base UID pointer from an event payload.
	 * @return Non-owning T*, or nullptr if null or the type does not match.
	 */
	template<typename T>
	static T* As(const UID* uid)
	{
		if (!uid) return nullptr;
		const ObjectID* oid = static_cast<const ObjectID*>(uid);
		if (oid->o_type != T::Type) return nullptr;
		return static_cast<T*>(const_cast<ObjectID*>(oid));
	}

	/**
	 * @brief Returns pointer to object's shader, if applicable.
	 * @return Void pointer cast to Shader*, or nullptr if no shader.
	 * @note Override in derived classes that own shaders (Mesh, Material, etc.).
	 */
	virtual void* GetShader()
	{
		return nullptr;
	}

	/**
	 * @brief Returns pointer to the ShaderUnit for a given shader stage, if applicable.
	 * @param shaderType Integer representing the ShaderType to query.
	 * @return Void pointer cast to ShaderUnit*, or nullptr if not found.
	 * @note Override in derived classes that own shaders (Mesh, Material, etc.).
	 */
	virtual void* GetShaderUnit(int /*shaderType*/) const
	{
		return nullptr;
	}

	/**
	 * @brief Returns pointer to object's transform, if applicable.
	 * @return Void pointer cast to Transform*, or nullptr if no transform.
	 * @note Override in derived classes with Transform component (Camera, Light, Mesh).
	 */
	virtual void* GetTransform()
	{
		return nullptr;
	}

	/**
	 * @brief Returns pointer to object's material, if applicable.
	 * @return Void pointer cast to Material*, or nullptr if no material.
	 * @note Override in derived classes with Material component (Mesh).
	 */
	virtual void* GetMaterial()
	{
		return nullptr;
	}

	/**
	 * @brief Constructs an ObjectID with default values.
	 */
	ObjectID() = default;

	/**
	 * @brief Destroys the ObjectID.
	 */
	~ObjectID() override = default;

	/**
	 * @brief Cereal serialization for object identity and metadata.
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::base_class<UID>(this), CEREAL_NVP(o_name), CEREAL_NVP(o_type),
		   CEREAL_NVP(is_viewport), CEREAL_NVP(is_rendered));
	}
};

} // namespace neurus
