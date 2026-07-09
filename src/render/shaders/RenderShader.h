/**
 * @file RenderShader.h
 * @brief Render shader - vertex+fragment pipeline with parse->generate->compile flow.
 *
 * RenderShader owns ShaderStruct IR representations for both vertex and fragment
 * stages, generated GLSL source code, and compiled ShaderModule objects (stored
 * in the base-class m_modules map).
 *
 * Ported from OpenGL project's RenderShader, adapted for Vulkan:
 *   - OpenGL:   CompileShaderCode() -> glCreateShader -> glCompileShader -> GLuint
 *   - Vulkan:   ShaderCompiler -> CompileGlslToSpv() -> ShaderModule (vk::raii)
 *
 * Architecture:
 *   - Inherits Shader (Task 6) - base class with m_modules cache.
 *   - Uses ShaderParser (Task 7) to populate ShaderStruct IRs.
 *   - Uses ShaderStruct::GenerateShader() (Task 8) to emit GLSL.
 *   - Uses ShaderCompiler (Task 4) for GLSL->SPIR-V compilation.
 *   - Uses ShaderModule (Task 5) to wrap vk::raii::ShaderModule.
 *
 * Lifecycle:
 *   1. Construct: RenderShader(name, vertPath, fragPath)
 *   2. Compile:   Compile(compiler) -> parse -> generate GLSL -> compile to SPIR-V
 *   3. Finalize:  SetDevice(device) -> create ShaderModule objects in m_modules
 *   4. Use:       GetVertexModule() / GetFragmentModule() for pipeline creation
 *   5. Recompile: Recompile(compiler, type) -> regenerate single stage, swap module
 *
 * @note ShaderModule creation is deferred until a device is available via
 *       SetDevice().  IsValid() returns true when SPIR-V compilation succeeded
 *       (SPIR-V vectors non-empty), even if ShaderModules haven't been created yet.
 */
#pragma once

#include "Shader.h"
#include "ShaderStruct.h"
#include "ShaderModule.h"

#include <memory>
#include <string>
#include <vector>

namespace neurus {

/**
 * @brief Standard render shader with vertex and fragment stages.
 *
 * RenderShader is the primary shader type for rasterisation rendering in Vulkan.
 * It manages the full shader compilation pipeline for both vertex and fragment
 * stages: parsing GLSL source into ShaderStruct IR, generating Vulkan GLSL,
 * compiling to SPIR-V via shaderc, and wrapping the result in ShaderModule
 * objects for pipeline creation.
 *
 * Usage:
 * @code
 *   RenderShader shader("PBR", "pbr.vert", "pbr.frag");
 *   shader.Compile(compiler);
 *   shader.SetDevice(device);
 *   auto vertMod = shader.GetVertexModule();
 *   auto fragMod = shader.GetFragmentModule();
 * @endcode
 *
 * @note Non-copyable (inherits from Shader).  Not thread-safe.
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
	RenderShader(const std::string& name, const std::string& vertPath, const std::string& fragPath);

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
	 * @brief Parses both shader files, generates GLSL, and compiles to SPIR-V.
	 *
	 * Flow:
	 *   1. ShaderParser::ParseShaderFile() for both vertex and fragment files
	 *      -> populates m_vertStruct and m_fragStruct IRs.
	 *   2. m_vertStruct.GenerateShader() and m_fragStruct.GenerateShader()
	 *      -> produces complete Vulkan GLSL source strings.
	 *   3. compiler.CompileGlslToSpv() for each stage
	 *      -> produces SPIR-V binary vectors (stored in m_vertSpirv / m_fragSpirv).
	 *
	 * ShaderModule objects are NOT created here (requires vk::raii::Device).
	 * Call SetDevice() after Compile() to populate m_modules.
	 *
	 * @param compiler ShaderCompiler instance for GLSL->SPIR-V compilation.
	 * @return true if both stages compiled successfully, false on parse/compile error.
	 */
	bool Compile(ShaderCompiler& compiler) override;

	/**
	 * @brief Returns true when both SPIR-V vectors are non-empty.
	 *
	 * Compilation success is indicated by non-empty SPIR-V vectors.
	 * ShaderModule objects may not yet be created (see SetDevice()).
	 */
	bool IsValid() const override;

	/**
	 * @brief Returns the primary shader type (VERTEX).
	 */
	ShaderType GetType() const override { return ShaderType::VERTEX; }

	// ----------------------------------
	// RenderShader-specific API
	// ----------------------------------

	/**
	 * @brief Returns a mutable reference to the ShaderStruct IR for a stage.
	 * @param type Must be ShaderType::VERTEX or ShaderType::FRAGMENT.
	 * @return Reference to the ShaderStruct.
	 * @note Returns m_vertStruct if type is unrecognised (safe fallback).
	 */
	ShaderStruct& GetStruct(ShaderType type);

	/**
	 * @brief Returns the compiled vertex ShaderModule from m_modules.
	 * @return Shared pointer, or nullptr if not yet created (call SetDevice() first).
	 */
	std::shared_ptr<ShaderModule> GetVertexModule();

	/**
	 * @brief Returns the compiled fragment ShaderModule from m_modules.
	 * @return Shared pointer, or nullptr if not yet created (call SetDevice() first).
	 */
	std::shared_ptr<ShaderModule> GetFragmentModule();

	/**
	 * @brief Regenerates and recompiles a single shader stage.
	 *
	 * Flow:
	 *   1. Re-generates GLSL from the ShaderStruct IR (if struct changed).
	 *   2. Re-compiles GLSL to SPIR-V via the compiler.
	 *   3. Swaps the old SPIR-V vector and ShaderModule (if device is set) with the new ones.
	 *
	 * @param compiler ShaderCompiler instance.
	 * @param type     Stage to recompile (VERTEX or FRAGMENT).
	 * @return true if the single-stage recompile succeeded.
	 */
	bool Recompile(ShaderCompiler& compiler, ShaderType type);

	/**
	 * @brief Sets the Vulkan device used to create ShaderModule objects.
	 *
	 * Must be called after Compile() to materialise the SPIR-V vectors
	 * into vk::raii::ShaderModule wrappers stored in m_modules.
	 *
	 * If modules already exist they are not re-created; call SetDevice()
	 * once before accessing modules.
	 *
	 * @param device Logical device that will own the ShaderModules.
	 */
	void SetDevice(const vk::raii::Device& device);

	/** @brief Returns the vertex shader file path. */
	const std::string& GetVertPath() const { return m_vertPath; }

	/** @brief Returns the fragment shader file path. */
	const std::string& GetFragPath() const { return m_fragPath; }

private:
	/**
	 * @brief Compiles a single shader stage from file to SPIR-V.
	 * @param compiler     ShaderCompiler instance.
	 * @param type         Shader stage (VERTEX or FRAGMENT).
	 * @param filepath     Path to the GLSL source file.
	 * @param shaderStruct [out] ShaderStruct IR to populate.
	 * @param outSpirv     [out] SPIR-V binary output vector.
	 * @return true on success.
	 */
	bool CompileStage(ShaderCompiler& compiler, ShaderType type,
	                  const std::string& filepath,
	                  ShaderStruct& shaderStruct,
	                  std::vector<uint32_t>& outSpirv);

	/**
	 * @brief Creates a ShaderModule from a SPIR-V vector using the stored device.
	 * @param spirv SPIR-V binary data.
	 * @return New ShaderModule, or nullptr if m_device is null.
	 */
	std::shared_ptr<ShaderModule> CreateModuleFromSpirv(const std::vector<uint32_t>& spirv);

	// ----------------------------------
	// Members
	// ----------------------------------

	ShaderStruct m_vertStruct; ///< Parsed vertex shader IR
	ShaderStruct m_fragStruct; ///< Parsed fragment shader IR

	std::string m_vertPath; ///< Path to vertex shader source file
	std::string m_fragPath; ///< Path to fragment shader source file

	std::vector<uint32_t> m_vertSpirv; ///< Compiled vertex SPIR-V binary
	std::vector<uint32_t> m_fragSpirv; ///< Compiled fragment SPIR-V binary

	const vk::raii::Device* m_device = nullptr; ///< Device for ShaderModule creation
};

} // namespace neurus
