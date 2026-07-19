#pragma once

#include <shaderc/shaderc.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace neurus {

/**
 * @brief Internal shaderc wrapper for GLSL-to-SPIR-V compilation.
 *
 * Owns a single shaderc::Compiler instance and a persistent set of
 * shaderc::CompileOptions that can be modified via the public API.
 *
 * This class is **internal** to src/render/shaders/ - only ShaderLibrary
 * should instantiate or use it.  Renderer passes and DeferredRenderer
 * never see this type.
 *
 * @note shaderc::Compiler is expensive to construct (initializes glslang
 *       internally), so keep one ShaderCompiler alive for the lifetime
 *       of the ShaderLibrary owner.
 */
class ShaderCompiler
{
public:
	/**
	 * @brief Creates the shaderc compiler and default compile options.
	 *
	 * Defaults:
	 *   - Target environment: Vulkan 1.3
	 *   - SPIR-V version:     1.6
	 *   - Optimization:       performance in Release, zero in Debug
	 */
	ShaderCompiler();
	~ShaderCompiler() = default;

	// Non-copyable - owns internal glslang state
	ShaderCompiler(const ShaderCompiler&) = delete;
	ShaderCompiler& operator=(const ShaderCompiler&) = delete;

	// Movable
	ShaderCompiler(ShaderCompiler&&) noexcept = default;
	ShaderCompiler& operator=(ShaderCompiler&&) noexcept = default;

	/**
	 * @brief Compiles a GLSL source string to SPIR-V bytecode.
	 *
	 * @param source     GLSL source text (null-terminated or length-delimited).
	 * @param kind       Shader kind (e.g. shaderc_vertex_shader).
	 * @param entryPoint Entry-point function name (default "main").
	 * @param inputName  Diagnostic file name shown in error messages.
	 * @return SPIR-V as a vector of uint32_t words.  Empty vector on failure;
	 *         call GetErrorMessage() for the compiler's error text.
	 */
	std::vector<uint32_t> CompileGlslToSpv(
		const std::string& source,
		shaderc_shader_kind kind,
		const std::string& entryPoint,
		const std::string& inputName);

	/**
	 * @brief Returns the compiler's last error message (clear-text).
	 *
	 * The message is reset on each successful compilation and populated
	 * on failure.
	 */
	const std::string& GetErrorMessage() const { return m_lastError; }

	/**
	 * @brief Sets the optimization level for subsequent compilations.
	 *
	 * shaderc_optimization_level_zero     - no optimization
	 * shaderc_optimization_level_size     - smallest code
	 * shaderc_optimization_level_performance - fastest code
	 */
	void SetOptimizationLevel(shaderc_optimization_level level);

	/**
	 * @brief Adds a preprocessor macro definition (e.g. "USE_SHADOWS" → "1").
	 *
	 * Macros are carried forward across compilations unless explicitly
	 * cleared.  Repeated calls with the same name overwrite the previous
	 * value.
	 *
	 * @param name  Macro name (without -D prefix).
	 * @param value Macro value (default "1").
	 */
	void AddMacroDefinition(const std::string& name, const std::string& value);

	/**
	 * @brief Controls whether debug info (source-level debugging) is
	 *        embedded in the compiled SPIR-V.
	 *
	 * Debug builds should emit debug info so tools like RenderDoc can
	 * display source-level shader debugging.
	 */
	void SetGenerateDebugInfo(bool enable);

private:
	shaderc::Compiler m_compiler;
	shaderc::CompileOptions m_options;
	std::string m_lastError;
};

} // namespace neurus
