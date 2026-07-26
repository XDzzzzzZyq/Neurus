/**
 * @file ComputeShader.h
 * @brief CPU-only compute shader using the parse->generate pipeline.
 *
 * ComputeShader manages a single compute-stage shader. Unlike RenderShader
 * (which manages vertex + fragment pairs), ComputeShader owns exactly one
 * ShaderUnit in m_stages[COMPUTE].
 *
 * Architecture:
 *   - Uses ShaderParser to populate ShaderStruct IR (single struct for compute).
 *   - Uses ShaderGenerator to produce generated GLSL code.
 *   - Per-instance default uniform configs (ported from OpenGL's static
 *     config_list pattern).
 *
 * Lifecycle:
 *   1. Construct with name + GLSL file path.
 *   2. Call ParseAndGenerate() -> parse -> generate.
 *   3. Access via GetStage() for IR or generated code.
 *   4. Recompile() to re-parse and regenerate.
 *
 * Usage:
 * @code
 *   ComputeShader cs("SSAO", "res/shaders/ssao.comp");
 *   cs.SetDefault("radius", 0.5f);
 *   cs.SetDefault("samples", 32);
 *   cs.ParseAndGenerate();
 *   auto& code = cs.GetGeneratedCode(ShaderType::COMPUTE);
 * @endcode
 *
 * @note Non-copyable (inherited from Shader). Movable.
 * @note Thread-safety: Not thread-safe. Must be used from the main thread.
 */

#pragma once

#include "Shader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neurus {

/**
 * @brief Compute shader with IR-based parse->generate pipeline.
 *
 * ComputeShader manages a single compute-stage shader. GPU compilation
 * (SPIR-V, ShaderModule creation) is handled externally by ShaderLibrary.
 */
class ComputeShader : public Shader
{
public:
	/**
	 * @brief Default uniform configuration entry.
	 *
	 * Ported from OpenGL's ComputeShader::Default pattern. Each entry
	 * describes a uniform parameter that should be set to a specific
	 * default value when the shader is instantiated.
	 */
	struct Default
	{
		std::string name;  ///< Uniform parameter name
		std::string type;  ///< GLSL type: "int", "float", or "bool"
		std::string value; ///< String representation of the default value
	};

	/**
	 * @brief Constructs a compute shader from a GLSL source file.
	 * @param name     Human-readable shader name (for logging and debugging).
	 * @param compPath Path to the compute shader GLSL source file (.comp).
	 */
	ComputeShader(std::string name, std::string compPath);

	~ComputeShader() override = default;

	// Non-copyable (inherits from Shader)
	ComputeShader(const ComputeShader&) = delete;
	ComputeShader& operator=(const ComputeShader&) = delete;

	// Movable
	ComputeShader(ComputeShader&&) noexcept = default;
	ComputeShader& operator=(ComputeShader&&) noexcept = default;

	// ----------------------------------
	// Shader interface (override)
	// ----------------------------------

	/**
	 * @brief Parses, generates the compute shader GLSL.
	 *
	 * Pipeline:
	 *   1. ShaderParser::ParseShaderFile(compPath, COMPUTE)
	 *   2. ShaderGenerator::Generate() -> regenerated GLSL source
	 *
	 * @return true if all steps succeeded, false on parse/generation error.
	 */
	bool ParseAndGenerate() override;

	/**
	 * @brief Returns the shader type.
	 * @return Always ShaderType::COMPUTE.
	 */
	ShaderType GetType() const override { return ShaderType::COMPUTE; }

	// ----------------------------------
	// Compute-specific API
	// ----------------------------------

	/**
	 * @brief Retrieves the compute workgroup size from the parsed GLSL.
	 *
	 * Reads local_size_x, local_size_y, local_size_z from the ShaderStruct IR
	 * stored in m_stages[COMPUTE].parsed.
	 *
	 * @param[out] x Workgroup size in X dimension.
	 * @param[out] y Workgroup size in Y dimension.
	 * @param[out] z Workgroup size in Z dimension.
	 */
	void GetWorkgroupSize(uint32_t& x, uint32_t& y, uint32_t& z) const;

	/**
	 * @brief Re-parses and re-generates the shader from source.
	 *
	 * CPU-only -- re-reads the source file, re-parses via ShaderParser,
	 * and re-generates GLSL via ShaderGenerator.
	 *
	 * @return true if re-parse and re-generation succeeded.
	 */
	bool Recompile();

	// ----------------------------------
	// Default uniform configurations (ported from OpenGL ComputeShader)
	// ----------------------------------

	/**
	 * @brief Sets a default value for a uniform parameter (int overload).
	 * @param name  Uniform parameter name.
	 * @param value Default integer value.
	 */
	void SetDefault(const std::string& name, int value);

	/**
	 * @brief Sets a default value for a uniform parameter (float overload).
	 * @param name  Uniform parameter name.
	 * @param value Default float value.
	 */
	void SetDefault(const std::string& name, float value);

	/**
	 * @brief Sets a default value for a uniform parameter (bool overload).
	 * @param name  Uniform parameter name.
	 * @param value Default boolean value.
	 */
	void SetDefault(const std::string& name, bool value);

	/**
	 * @brief Returns the vector of default uniform configurations.
	 *
	 * These are per-instance (not static like the OpenGL version).
	 * Each entry describes a uniform name, its GLSL type, and default value.
	 */
	const std::vector<Default>& GetDefaults() const { return m_defaults; }

private:
	std::string m_compPath;          ///< Path to the original GLSL source file
	std::vector<Default> m_defaults; ///< Per-instance default uniform configs
};

} // namespace neurus
