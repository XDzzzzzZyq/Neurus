#pragma once

#include "TextureLib.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace neurus {

/**
 * @brief Material descriptor for PBR rendering.
 *
 * Material encapsulates all surface appearance parameters for physically-based
 * rendering. Each parameter can be a float constant, a color (vec3), or a
 * texture map, discriminated by MatDataType.
 *
 * Material Parameters:
 * - MAT_ALBEDO:   Base color / diffuse albedo
 * - MAT_METAL:    Metallic factor (0 = dielectric, 1 = conductor)
 * - MAT_ROUGH:    Roughness (0 = smooth/mirror, 1 = rough/matte)
 * - MAT_SPEC:     Specular reflectance at normal incidence (F0)
 * - MAT_EMIS_COL: Emissive color
 * - MAT_EMIS_STR: Emissive strength multiplier
 * - MAT_ALPHA:    Opacity / transparency
 * - MAT_NORMAL:   Normal map (tangent-space)
 * - MAT_BUMP:     Bump / height map
 *
 * Usage:
 * @code
 *   Material mat;
 *   mat.SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f, 0.766f, 0.336f));
 *   mat.SetMatParam(Material::MAT_METAL, 1.0f);
 *   mat.SetMatParam(Material::MAT_ROUGH, 0.3f);
 * @endcode
 *
 * @note Not thread-safe. Material state must not be modified during rendering.
 */
class Material
{
public:
	// ---------------------------------------------------------------------------
	// Enums
	// ---------------------------------------------------------------------------

	/**
	 * @brief Material parameter types.
	 *
	 * Indexed so that MAT_END can serve as a count / sentinel.
	 */
	enum MatParaType
	{
		MAT_NONE = -1,   ///< Invalid / uninitialized
		MAT_ALBEDO,      ///< Base colour (vec3 or texture)
		MAT_METAL,       ///< Metallic factor (float or texture)
		MAT_ROUGH,       ///< Roughness factor (float or texture)
		MAT_SPEC,        ///< Specular F0 (vec3 or texture)
		MAT_EMIS_COL,    ///< Emissive colour (vec3 or texture)
		MAT_EMIS_STR,    ///< Emissive strength (float)
		MAT_ALPHA,       ///< Opacity (float or texture)
		MAT_NORMAL,      ///< Normal map (texture, tangent-space)
		MAT_BUMP,        ///< Bump / height map (texture)
		MAT_END          ///< Sentinel — number of parameter types
	};

	/**
	 * @brief Data-type discriminator for a material parameter.
	 */
	enum MatDataType
	{
		MPARA_FLT,  ///< Float constant
		MPARA_COL,  ///< Colour constant (vec3)
		MPARA_TEX   ///< Texture map (sampler2D)
	};

	// ---------------------------------------------------------------------------
	// Type aliases
	// ---------------------------------------------------------------------------

	/**
	 * @brief Single parameter slot: (data-type, float-value, colour-value, texture).
	 *
	 * Only the member corresponding to the current MatDataType is meaningful.
	 */
	using MatParamData = std::tuple<MatDataType, float, glm::vec3, TextureLib::TextureRes>;

	/** @brief Shared pointer to a Material. */
	using MaterialRes = std::shared_ptr<Material>;

	// ---------------------------------------------------------------------------
	// Static uniform name table
	// ---------------------------------------------------------------------------

	/**
	 * @brief Maps each MatParaType to its corresponding shader uniform name.
	 *
	 * Indexed by MatParaType value (MAT_ALBEDO = 0, …, MAT_BUMP = 8).
	 * Initialised in InitParamData().
	 */
	static std::vector<std::string> mat_uniform_name;

	// ---------------------------------------------------------------------------
	// Public data
	// ---------------------------------------------------------------------------

	/** @brief Human-readable material name (for debugging and serialization). */
	std::string mat_name{ "Default" };

	/** @brief Per-parameter storage. Populated by InitParamData(). */
	std::unordered_map<MatParaType, MatParamData> mat_params;

	// ---------------------------------------------------------------------------
	// Lifecycle
	// ---------------------------------------------------------------------------

	/**
	 * @brief Constructs a Material and calls InitParamData().
	 */
	Material();

	/**
	 * @brief Virtual destructor (dtor is default).
	 */
	~Material() = default;

	// Non-copyable, movable
	Material(const Material&) = delete;
	Material& operator=(const Material&) = delete;
	Material(Material&&) noexcept = default;
	Material& operator=(Material&&) noexcept = default;

	// ---------------------------------------------------------------------------
	// Initialisation
	// ---------------------------------------------------------------------------

	/**
	 * @brief Initialises every MatParaType with sensible PBR defaults.
	 *
	 * Defaults:
	 * - Albedo:     white (1, 1, 1)
	 * - Metallic:   0.0
	 * - Roughness:  0.5
	 * - Specular:   (0.04, 0.04, 0.04)  — dielectric F0
	 * - Emissive:   black, strength 0
	 * - Alpha:      1.0 (opaque)
	 *
	 * Called automatically by the constructor.
	 */
	void InitParamData();

	// ---------------------------------------------------------------------------
	// Parameter setters
	// ---------------------------------------------------------------------------

	/**
	 * @brief Sets the data type for a parameter (without changing its value).
	 * @param _tar Target parameter.
	 * @param _type New data type.
	 */
	void SetMatParam(MatParaType _tar, MatDataType _type);

	/**
	 * @brief Sets a parameter to a float constant.
	 * @param _tar Target parameter.
	 * @param _var Float value.
	 */
	void SetMatParam(MatParaType _tar, float _var);

	/**
	 * @brief Sets a parameter to a colour constant.
	 * @param _tar Target parameter.
	 * @param _col RGB colour (values in [0, 1]).
	 */
	void SetMatParam(MatParaType _tar, const glm::vec3& _col);

	/**
	 * @brief Sets a parameter to a texture map.
	 * @param _tar Target parameter.
	 * @param _tex Shared pointer to the Texture.
	 */
	void SetMatParam(MatParaType _tar, TextureLib::TextureRes _tex);

	// ---------------------------------------------------------------------------
	// Configuration loading
	// ---------------------------------------------------------------------------

	/**
	 * @brief Factory: creates a Material from a config file path.
	 *
	 * If _path is empty a default Material is returned.
	 *
	 * @param _path Path to a material config file (JSON or custom format).
	 * @return Shared pointer to the loaded (or default) Material.
	 */
	static MaterialRes LoadMaterial(std::string _path = "");

	/**
	 * @brief Parses a configuration string and updates mat_params.
	 *
	 * @param _config Material configuration string.
	 *
	 * @note Stub — actual JSON/file parsing will be added post-MVP.
	 */
	void ParseConfig(const std::string& _config);

	// ---------------------------------------------------------------------------
	// Texture binding
	// ---------------------------------------------------------------------------

	/**
	 * @brief Binds all material textures to their respective shader slots.
	 *
	 * Iterates through mat_params and binds every parameter with
	 * MatDataType == MPARA_TEX.
	 *
	 * @note Stub for MVP. Actual Vulkan texture binding happens in the render
	 *       pass. This method currently returns without performing any work.
	 */
	void BindMatTexture() const;

	// ---------------------------------------------------------------------------
	// Dirty flags
	// ---------------------------------------------------------------------------

	/** @brief True when any parameter value has changed (triggers uniform upload). */
	bool is_mat_changed{ true };

	/** @brief True when parameter data types have changed (triggers descriptor rebuild). */
	bool is_mat_struct_changed{ true };
};

} // namespace neurus
