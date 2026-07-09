#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace neurus {

// Forward declarations - no heavy includes in this header
class ShaderCompiler;
class ShaderModule;

/**
 * @brief Shader stage types supported by the renderer.
 */
enum class ShaderType
{
	VERTEX = 0,   ///< Vertex shader stage
	FRAGMENT = 1, ///< Fragment (pixel) shader stage
	COMPUTE = 2,  ///< Compute shader stage (GPGPU)
	GEOMETRY = 3  ///< Geometry shader stage
};

/**
 * @brief Abstract base class for all shader types in the dynamic shader system.
 *
 * Shader defines the common interface for compilation and module access.
 * Subclasses (RenderShader, ComputeShader) implement stage-specific compilation
 * and linking logic.
 *
 * Lifecycle:
 * 1. Construct shader with name and source
 * 2. Call Compile(compiler) to produce ShaderModule objects
 * 3. Access modules via GetShaderModule() for pipeline creation
 * 4. Destroy shader (shared_ptr-managed via ShaderLibrary)
 *
 * @note Non-copyable (owns GPU resources indirectly via ShaderModule).
 * @note Thread-safety: Not thread-safe. Must be used from the main thread.
 */
class Shader
{
public:
	/**
	 * @brief Constructs a shader with a name and GLSL source code.
	 * @param name Human-readable shader name (for logging and debugging).
	 * @param source GLSL source code string.
	 */
	Shader(std::string name, std::string source);
	virtual ~Shader() = default;

	// Non-copyable - shader modules are owned by derived classes
	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;

	// Movable
	Shader(Shader&&) noexcept = default;
	Shader& operator=(Shader&&) noexcept = default;

	/**
	 * @brief Compiles all shader stages using the provided compiler.
	 *
	 * Derived classes implement stage-specific compilation (vertex, fragment,
	 * compute, etc.) and populate the m_modules map with resulting ShaderModule
	 * objects.
	 *
	 * @param compiler ShaderCompiler instance for GLSL->SPIR-V compilation.
	 * @return true if all stages compiled successfully, false otherwise.
	 */
	virtual bool Compile(ShaderCompiler& compiler) = 0;

	/**
	 * @brief Checks whether the shader is in a valid, compiled state.
	 * @return true if all required stages are compiled and valid.
	 */
	virtual bool IsValid() const = 0;

	/**
	 * @brief Returns the primary shader type.
	 * @return The ShaderType this shader represents.
	 */
	virtual ShaderType GetType() const = 0;

	/**
	 * @brief Retrieves a compiled ShaderModule for a specific stage.
	 * @param type The shader stage to look up.
	 * @return Shared pointer to the ShaderModule, or nullptr if not found.
	 */
	std::shared_ptr<ShaderModule> GetShaderModule(ShaderType type) const;

	/**
	 * @brief Converts a ShaderType to its string representation.
	 * @param type The shader stage type.
	 * @return String name (e.g., "VERTEX", "FRAGMENT", "COMPUTE", "GEOMETRY").
	 */
	static std::string TypeToString(ShaderType type);

	/** @brief Returns the shader name. */
	const std::string& GetName() const { return m_name; }

	/** @brief Returns the GLSL source code. */
	const std::string& GetSource() const { return m_source; }

	/** @brief Returns the last compilation error message, if any. */
	const std::string& GetErrorMessage() const { return m_errorMessage; }

protected:
	std::string m_name;          ///< Human-readable shader name
	std::string m_source;        ///< GLSL source code
	std::string m_errorMessage;  ///< Last compilation error message

	/** Map of compiled ShaderModules, keyed by ShaderType. */
	std::unordered_map<ShaderType, std::shared_ptr<ShaderModule>> m_modules;
};

} // namespace neurus
