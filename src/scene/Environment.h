/**
 * @file Environment.h
 * @brief IBL (Image-Based Lighting) environment map object for the scene.
 *
 * Environment represents a dome light / skybox that provides image-based
 * lighting via diffuse irradiance and specular prefiltered cubemaps.
 * It owns file path references to the equirectangular HDR source and
 * runtime dirty tracking for GPU resource reload.
 *
 * Architecture:
 * - Owned by scene graph (shared_ptr in Scene containers)
 * - Renderer reads diffuse/specular Texture pointers as immutable
 * - Editor mutates environment properties via events and controllers
 * - Dirty flag triggers cubemap regeneration on the GPU
 *
 * @note Texture pointers are GPU resources owned by the Renderer.
 *       Serialization stores only the equirectangular path, not GPU textures.
 * @note Only one Environment is typically active per scene.
 */

#pragma once

#include <memory>
#include <string>

#include "UID.h"
#include "Transform.h"

namespace neurus
{

// --- Forward declarations -------------------------------------------------

class Texture;

/**
 * @brief IBL environment map providing image-based lighting for the scene.
 *
 * Environment stores file paths and parameters for an HDR equirectangular
 * map that is converted into a diffuse irradiance cubemap and a specular
 * prefiltered cubemap at load time. The Texture pointers reference GPU
 * resources managed by the Renderer.
 *
 * Resource Ownership:
 * - diffuse_texture: Non-owning pointer (owned by Renderer's GpuResourceCache)
 * - specular_texture: Non-owning pointer (owned by Renderer's GpuResourceCache)
 * - m_equirectPath:   Owned string (serialized, used for GPU reload)
 * - Transform:        Owned directly (each environment has unique transform
 *                     via Transform3D for skybox orientation)
 *
 * @note Inheritance: ObjectID for scene identity, Transform3D for rotation.
 * @note Thread-safety: Not thread-safe. Access from main thread only.
 */
class Environment : public ObjectID, public Transform3D
{
public:
	/// Shared ownership type for scene graph containers
	using Resource = std::shared_ptr<Environment>;

	/**
	 * @brief Constructs an Environment with default IBL parameters.
	 */
	Environment();

	/**
	 * @brief Virtual destructor for polymorphic cleanup.
	 */
	~Environment() override = default;

	// Non-copyable (like all scene objects)
	Environment(const Environment&) = delete;
	Environment& operator=(const Environment&) = delete;

	// -----------------------------------------------------------------------
	// IBL data (non-owning GPU resource pointers)
	// -----------------------------------------------------------------------

	/** @brief Diffuse irradiance cubemap (64px, 1 mip level). */
	Texture* diffuse_texture = nullptr;

	/** @brief Specular prefiltered cubemap (2048px, 8 mip levels). */
	Texture* specular_texture = nullptr;

	// -----------------------------------------------------------------------
	// File path
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the equirectangular HDR source file path.
	 * @param path Path to the .hdr equirectangular map.
	 * @note Marks the environment as dirty for GPU reload.
	 */
	void SetEquirectPath(const std::string& path);

	/**
	 * @brief Returns the current equirectangular HDR source file path.
	 * @return Const reference to the path string.
	 */
	const std::string& GetEquirectPath() const;

	// -----------------------------------------------------------------------
	// Intensity
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the IBL intensity multiplier.
	 * @param i Intensity scale factor (1.0 = physical).
	 */
	void SetIntensity(float i);

	/**
	 * @brief Returns the current IBL intensity multiplier.
	 * @return Intensity scale factor.
	 */
	float GetIntensity() const;

	// -----------------------------------------------------------------------
	// Rotation
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the environment map rotation (Y-axis).
	 * @param r Rotation angle in degrees around the Y (up) axis.
	 */
	void SetRotation(float r);

	/**
	 * @brief Returns the current environment map rotation.
	 * @return Rotation angle in degrees.
	 */
	float GetRotation() const;

	// -----------------------------------------------------------------------
	// Dirty tracking
	// -----------------------------------------------------------------------

	/**
	 * @brief Returns whether the environment has been modified since last GPU sync.
	 * @return True if dirty and needs GPU resource reload.
	 */
	bool IsDirty() const;

	/**
	 * @brief Clears the dirty flag after GPU resource sync.
	 */
	void ClearDirty();

	// -----------------------------------------------------------------------
	// Virtual overrides (ObjectID polymorphic accessors)
	// -----------------------------------------------------------------------

	/**
	 * @brief Returns typed pointer to this object's Transform component.
	 * @return Void pointer to Transform3D (inherited from Transform3D).
	 * @note Overrides ObjectID::GetTransform() for polymorphic transform access.
	 */
	void* GetTransform() override
	{
		return static_cast<Transform3D*>(this);
	}

	/**
	 * @brief Environment does not own a shader.
	 * @return nullptr always.
	 */
	void* GetShader() override
	{
		return nullptr;
	}

	/**
	 * @brief Environment does not own a material.
	 * @return nullptr always.
	 */
	void* GetMaterial() override
	{
		return nullptr;
	}

	// -----------------------------------------------------------------------
	// Serialization (Cereal)
	// -----------------------------------------------------------------------

	/**
	 * @brief Cereal serialization for Environment.
	 *
	 * Only file path, intensity, and rotation are serialized.
	 * Texture pointers (GPU resources) are NOT serialized.
	 *
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(CEREAL_NVP(m_equirectPath),
		   CEREAL_NVP(m_intensity),
		   CEREAL_NVP(m_rotation));
	}

private:
	std::string m_equirectPath;       ///< Source equirectangular HDR file path
	float       m_intensity = 1.0f;   ///< IBL intensity multiplier
	float       m_rotation  = 0.0f;   ///< Y-axis rotation in degrees
	bool        m_dirty     = false;  ///< Dirty flag for GPU resource reload
};

} // namespace neurus
