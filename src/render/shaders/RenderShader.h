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
	 * @param name     Human-readable shader name (for logging and cache keys).
	 * @param vertPath Path to the vertex shader GLSL source file.
	 * @param fragPath Path to the fragment shader GLSL source file.
	 */
	RenderShader(std::string name, std::string vertPath, std::string fragPath);

	~RenderShader() override = default;

	// Non-copyable (inherits from Shader)
	RenderShader(const RenderShader&) = delete;
	RenderShader& operator=(const RenderShader&) = delete;

	// Movable
	RenderShader(RenderShader&&) noexcept = default;
	RenderShader& operator=(RenderShader&&) noexcept = default;

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
