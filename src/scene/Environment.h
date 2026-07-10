/**
 * @file Environment.h
 * @brief IBL (Image-Based Lighting) environment map object for the scene.
 *
 * Environment represents a dome light / skybox that provides image-based
 * lighting via diffuse irradiance and specular prefiltered cubemaps.
 * It stores the source equirectangular HDR as CPU-side ImageData and
 * serialisable parameters (path, intensity, rotation).
 *
 * CPU-only - GPU resources are owned by RenderCache (EnvironmentGPU).
 *
 * Architecture:
 * - Owned by scene graph (shared_ptr in Scene containers)
 * - GPU cubemaps are created lazily by RenderCache::CreateEnvironmentGPU()
 * - Renderer reads EnvironmentGPU from RenderCache per-frame
 * - Editor mutates environment properties via events and controllers
 *
 * @note Serialization stores only the equirectangular path, not GPU textures.
 * @note Only one Environment is typically active per scene.
 */

#pragma once

#include <string>

#include "UID.h"
#include "Transform.h"
#include "asset/ImageData.h"

namespace neurus
{

/**
 * @brief IBL environment map providing image-based lighting for the scene.
 *
 * Environment stores file paths, parameters, and CPU-side pixel data for
 * an HDR equirectangular map.  GPU resources (cubemaps, samplers) are
 * owned by RenderCache and created lazily via CreateEnvironmentGPU().
 *
 * Resource Ownership:
 * - o_equirectData:   Owned ImageData (CPU-side equirectangular pixels)
 * - o_equirectPath:   Owned string (serialized, used for GPU reload)
 * - Transform3D:      Owned directly (skybox orientation rotation)
 *
 * @note Inheritance: ObjectID for scene identity, Transform3D for rotation.
 * @note Thread-safety: Not thread-safe. Access from main thread only.
 */
class Environment : public ObjectID, public Transform3D
{
public:
	/**
	 * @brief Constructs an Environment with default IBL parameters.
	 */
	Environment();

	/**
	 * @brief Virtual destructor for polymorphic cleanup.
	 */
	~Environment() override;

	// Non-copyable (like all scene objects)
	Environment(const Environment&) = delete;
	Environment& operator=(const Environment&) = delete;

	// -----------------------------------------------------------------------
	// Equirectangular data (CPU-side)
	// -----------------------------------------------------------------------

	/**
	 * @brief Returns a const reference to the CPU-side equirectangular pixel data.
	 * @note Loaded from o_equirectPath on construction or via SetEquirectPath().
	 */
	const ImageData& GetEquirectData() const { return o_equirectData; }

	/**
	 * @brief Directly sets the CPU-side equirectangular pixel data (for tests/procedural data).
	 * @param data ImageData to copy into the environment.
	 * @note Bypasses file loading; not serialized.
	 */
	void SetEquirectData(const ImageData& data) { o_equirectData = data; }

	// -----------------------------------------------------------------------
	// File path
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the equirectangular HDR source file path and reloads ImageData.
	 * @param path Path to the .hdr equirectangular map.
	 */
	void SetEquirectPath(const std::string& path);

	/**
	 * @brief Returns the current equirectangular HDR source file path.
	 * @return Const reference to the path string.
	 */
	const std::string& GetEquirectPath() const { return o_equirectPath; }

	// -----------------------------------------------------------------------
	// Intensity
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the IBL intensity multiplier.
	 * @param i Intensity scale factor (1.0 = physical).
	 */
	void SetIntensity(float i) { o_intensity = i; }

	/**
	 * @brief Returns the current IBL intensity multiplier.
	 * @return Intensity scale factor.
	 */
	float GetIntensity() const { return o_intensity; }

	// -----------------------------------------------------------------------
	// Rotation
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the environment map rotation (Y-axis).
	 * @param r Rotation angle in degrees around the Y (up) axis.
	 */
	void SetRotation(float r) { o_rotation = r; }

	/**
	 * @brief Returns the current environment map rotation.
	 * @return Rotation angle in degrees.
	 */
	float GetRotation() const { return o_rotation; }

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
	 * Serializes ObjectID identity and transform through base class
	 * serialization, along with environment-specific properties.
	 * GPU textures are NOT serialized (they live in RenderCache).
	 *
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::base_class<ObjectID>(this),
		   cereal::make_nvp("transform", cereal::base_class<Transform3D>(this)),
		   cereal::make_nvp("m_equirectPath", o_equirectPath),
		   cereal::make_nvp("m_intensity", o_intensity),
		   cereal::make_nvp("m_rotation", o_rotation));
	}

private:
	std::string o_equirectPath;       ///< Source equirectangular HDR file path
	float       o_intensity = 1.0f;   ///< IBL intensity multiplier
	float       o_rotation  = 0.0f;   ///< Y-axis rotation in degrees

	ImageData   o_equirectData;       ///< CPU-side equirectangular pixel data
};

} // namespace neurus
