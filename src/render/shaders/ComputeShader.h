/**
 * @file ComputeShader.h
 * @brief Compute shader implementation using the full parse->generate->compile pipeline.
 *
 * Unlike the OpenGL version which bypasses parsing, Neurus ComputeShader uses
 * the complete dynamic shader pipeline:
 *   1. Parse GLSL source -> ShaderStruct IR
 *   2. GenerateShader() -> regenerated Vulkan GLSL with local_size layout
 *   3. CompileGlslToSpv() -> SPIR-V bytecode
 *
 * Architecture:
 *   - Owns a ShaderStruct for the parsed IR (single struct for compute stage).
 *   - Stores compiled SPIR-V; ShaderModule creation is deferred until a
 *     Vulkan device is available via CreateModule().
 *   - Per-instance default uniform configs (ported from OpenGL's static
 *     config_list pattern).
 *
 * @note ShaderModule creation requires a vk::raii::Device. Call CreateModule()
 *       after Compile() once a device is available.
 */

#pragma once

#include "Shader.h"
#include "ShaderStruct.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Forward-declare Vulkan device type for CreateModule() parameter.
// The full vulkan/vulkan_raii.hpp include is only needed in the .cpp file.
namespace vk { namespace raii { class Device; } }

namespace neurus {

class ShaderCompiler;
class ShaderModule;

/**
 * @brief Compute shader with full IR-based compilation pipeline.
 *
 * ComputeShader manages a single compute-stage shader. Unlike RenderShader
 * (which manages vertex + fragment pairs), ComputeShader owns exactly one
 * ShaderStruct and produces a single ShaderModule.
 *
 * Lifecycle:
 *   1. Construct with name + GLSL file path.
 *   2. Call Compile(compiler) -> parse -> generate -> SPIR-V.
 *   3. Call CreateModule(device) -> ShaderModule from SPIR-V.
 *   4. Access via GetModule() for pipeline creation.
 *   5. Recompile(compiler) to re-parse and regenerate after IR changes.
 *
 * Usage:
 * @code
 *   ComputeShader cs("SSAO", "res/shaders/ssao.comp");
 *   cs.SetDefault("radius", 0.5f);
 *   cs.SetDefault("samples", 32);
 *   cs.Compile(compiler);
 *   cs.CreateModule(device);
 *   auto module = cs.GetModule();
 * @endcode
 *
 * @note Non-copyable (inherited from Shader). Movable.
 * @note Thread-safety: Not thread-safe. Must be used from the main thread.
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
	ComputeShader(const std::string& name, const std::string& compPath);

	// -------------------------------------------------------------------
	// Shader interface (override)
	// -------------------------------------------------------------------

	/**
	 * @brief Parses, generates, and compiles the compute shader to SPIR-V.
	 *
	 * Pipeline:
	 *   1. ShaderParser::ParseShaderFile(compPath, COMPUTE, m_struct)
	 *   2. m_struct.GenerateShader() -> regenerated GLSL source
	 *   3. compiler.CompileGlslToSpv(source, shaderc_glsl_compute_shader, "main", name)
	 *   4. Store resulting SPIR-V bytecode
	 *
	 * @param compiler ShaderCompiler instance for GLSL->SPIR-V compilation.
	 * @return true if all steps succeeded, false on parse/generation/compile error.
	 */
	bool Compile(ShaderCompiler& compiler) override;

	/**
	 * @brief Checks whether the shader is in a valid, compiled state.
	 * @return true if SPIR-V bytecode is non-empty (compilation succeeded).
	 */
	bool IsValid() const override;

	/**
	 * @brief Returns the shader type.
	 * @return Always ShaderType::COMPUTE.
	 */
	ShaderType GetType() const override { return ShaderType::COMPUTE; }

	// -------------------------------------------------------------------
	// Compute-specific API
	// -------------------------------------------------------------------

	/**
	 * @brief Returns a mutable reference to the parsed ShaderStruct IR.
	 *
	 * Modifications to the struct will take effect on the next
	 * Recompile() call. Useful for injecting build-in constants or
	 * modifying shader behaviour at runtime.
	 */
	ShaderStruct& GetStruct() { return m_struct; }

	/** @brief Returns a const reference to the parsed ShaderStruct IR. */
	const ShaderStruct& GetStruct() const { return m_struct; }

	/**
	 * @brief Returns the compiled ShaderModule for this compute shader.
	 *
	 * Compute shaders have a single stage; this is a convenience wrapper
	 * around GetShaderModule(ShaderType::COMPUTE).
	 *
	 * @return Shared pointer to the ShaderModule, or nullptr if
	 *         CreateModule() has not been called yet.
	 */
	std::shared_ptr<ShaderModule> GetModule();

	/**
	 * @brief Creates the ShaderModule from the stored SPIR-V bytecode.
	 *
	 * Must be called after Compile() and before GetModule(). Requires a
	 * valid Vulkan device for shader module creation.
	 *
	 * @param device Logical device for shader module creation.
	 * @return true if the module was created successfully.
	 */
	bool CreateModule(const vk::raii::Device& device);

	/**
	 * @brief Retrieves the compute workgroup size from the parsed GLSL.
	 *
	 * Reads local_size_x, local_size_y, local_size_z from the ShaderStruct IR.
	 * These values come from `layout(local_size_x = X, local_size_y = Y,
	 * local_size_z = Z) in;` in the source GLSL.
	 *
	 * @param[out] x Workgroup size in X dimension.
	 * @param[out] y Workgroup size in Y dimension.
	 * @param[out] z Workgroup size in Z dimension.
	 */
	void GetWorkgroupSize(uint32_t& x, uint32_t& y, uint32_t& z) const;

	/**
	 * @brief Re-generates and re-compiles the shader from the current IR state.
	 *
	 * Calls GenerateShader() on the current m_struct (which may have been
	 * modified via GetStruct() since the last compile) and recompiles to
	 * SPIR-V. The existing SPIR-V is replaced on success.
	 *
	 * @param compiler ShaderCompiler instance for GLSL->SPIR-V compilation.
	 * @return true if regeneration and recompilation succeeded.
	 */
	bool Recompile(ShaderCompiler& compiler);

	// -------------------------------------------------------------------
	// Default uniform configurations (ported from OpenGL ComputeShader)
	// -------------------------------------------------------------------

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
	std::string m_compPath;              ///< Path to the original GLSL source file
	ShaderStruct m_struct;               ///< Parsed IR (populated by Compile)
	std::vector<uint32_t> m_spirv;       ///< Compiled SPIR-V bytecode
	std::string m_generatedSource;       ///< GLSL source generated from IR
	std::vector<Default> m_defaults;     ///< Per-instance default uniform configs
};

} // namespace neurus
