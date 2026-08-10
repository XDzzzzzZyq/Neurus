/**
 * @file RenderShader.h
 * @brief CPU-only render shader - vertex+fragment pipeline with parse->generate flow.
 *
 * RenderShader owns ShaderUnit IR representations for both vertex and fragment
 * stages via the base-class m_stages map, and generated GLSL source code.
 *
 * Architecture:
 *   - Inherits Shader - base class with m_stages ShaderUnit map.
 *   - Uses ShaderParser to populate ShaderStruct IRs.
 *   - Uses ShaderGenerator to emit GLSL source.
 *
 * Lifecycle:
 *   1. Construct: RenderShader(name, vertPath, fragPath)
 *   2. ParseAndGenerate: parse -> generate GLSL for both stages
 *   3. Access: GetStage(), GetParsedStruct(), GetGeneratedCode()
 *   4. Recompile: Recompile(type) -> re-parse single stage, re-generate
 */
#pragma once

#include "Shader.h"

namespace neurus {

/**
 * @brief Standard render shader with vertex and fragment stages.
 *
 * RenderShader is the primary shader type for rasterisation rendering.
 * It manages parsing GLSL source into ShaderStruct IR and generating
 * Vulkan GLSL for both vertex and fragment stages. All GPU compilation
 * and module creation is handled externally by ShaderLibrary.
 *
 * Usage:
 * @code
 *   RenderShader shader("PBR", "pbr.vert", "pbr.frag");
 *   shader.ParseAndGenerate();
 *   auto& vertCode = shader.GetGeneratedCode(ShaderType::VERTEX);
 * @endcode
 *
 * @note Non-copyable (inherits from Shader). Movable. Not thread-safe.
 */
class RenderShader : public Shader
{
public:
	/**
	 * @brief Constructs a render shader with file paths to GLSL source.
	 *
	 * Default arguments make this the default ctor used by cereal's
	 * polymorphic load; content loads later via ReloadContent() (pool)
	 * or an explicit ParseAndGenerate() call (ShaderLibrary).
	 *
	 * @param name     Human-readable shader name (for logging and cache keys).
	 * @param vertPath Path to the vertex shader GLSL source file.
	 * @param fragPath Path to the fragment shader GLSL source file.
	 */
	RenderShader(std::string name = "", std::string vertPath = "", std::string fragPath = "");

	~RenderShader() override = default;

	// Non-copyable / non-movable (inherits UID semantics).
	RenderShader(const RenderShader&) = delete;
	RenderShader& operator=(const RenderShader&) = delete;
	RenderShader(RenderShader&&) = delete;
	RenderShader& operator=(RenderShader&&) = delete;

	/**
	 * @brief (Re)loads both shader stages from disk.
	 *
	 * Called at the end of serialize(load) so a pooled RenderShader restores
	 * its parsed IR + generated code. No-op for empty paths (identity shell).
	 */
	void ReloadContent();

	/**
	 * @brief Compiles every stage's GLSL to SPIR-V in place and bumps versions.
	 *
	 * Mirrors the runtime create flow (Editor::OnCreateShader): each stage's
	 * `spv` is built from its generated `code` and the stage version bumped;
	 * Shader::m_version bumps once if every present stage compiled. Stages
	 * that already hold SPIR-V are skipped (idempotent).
	 *
	 * Used after project load, where serialization restores only the source
	 * paths and re-parses content but never recompiles - without this,
	 * GeometryPass cannot create the per-mesh pipeline.
	 *
	 * @return true if every present stage compiled to non-empty SPIR-V.
	 */
	bool CompileToSpv();

	/**
	 * @brief Cereal serialization - name (via Shader) + both source paths,
	 *        then content reload (re-parse both stages) + SPIR-V recompile.
	 *
	 * Load restores the object's own state per the ResourceManager design
	 * (each pooled object's serialize(load) restores its content): paths are
	 * restored, ReloadContent() re-parses the stages from disk, and
	 * CompileToSpv() regenerates the derived SPIR-V so GeometryPass can build
	 * the per-mesh pipeline immediately (no post-load recompile step).
	 *
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::base_class<Shader>(this), CEREAL_NVP(m_vertPath), CEREAL_NVP(m_fragPath));
		if constexpr (Archive::is_loading::value)
		{
			ReloadContent();
			CompileToSpv();
		}
	}

	// ----------------------------------
	// Shader interface (override)
	// ----------------------------------

	/**
	 * @brief Parses both shader files and generates GLSL code.
	 *
	 * Flow:
	 *   1. ShaderParser::ParseShaderFile() for both vertex and fragment files
	 *      -> populates ShaderStruct IRs in m_stages.
	 *   2. ShaderGenerator::Generate() for each stage
	 *      -> produces complete Vulkan GLSL source strings.
	 *
	 * @return true if both stages parsed and generated successfully.
	 */
	bool ParseAndGenerate() override;

	/**
	 * @brief Returns the primary shader type (VERTEX).
	 */
	ShaderType GetType() const override { return ShaderType::VERTEX; }

	// ----------------------------------
	// RenderShader-specific API
	// ----------------------------------

	/** @brief Convenience: returns the vertex ShaderUnit. */
	ShaderUnit& GetVertex() { return GetStage(ShaderType::VERTEX); }

	/** @brief Convenience: returns the fragment ShaderUnit. */
	ShaderUnit& GetFragment() { return GetStage(ShaderType::FRAGMENT); }

	/** @brief Returns the vertex shader file path. */
	const std::string& GetVertPath() const;

	/** @brief Returns the fragment shader file path. */
	const std::string& GetFragPath() const;

	/**
	 * @brief Re-parses and re-generates a single shader stage.
	 *
	 * Re-reads the source file for the given type, re-parses into the
	 * ShaderStruct IR, and re-generates GLSL code. CPU-only -- no
	 * GPU compilation takes place.
	 *
	 * @param type Stage to recompile (VERTEX or FRAGMENT).
	 * @return true if re-parse and re-generation succeeded.
	 */
	bool Recompile(ShaderType type);

private:
	std::string m_vertPath; ///< Path to vertex shader source file
	std::string m_fragPath; ///< Path to fragment shader source file
};

} // namespace neurus
