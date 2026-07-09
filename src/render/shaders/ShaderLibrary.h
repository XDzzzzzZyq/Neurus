#pragma once

#include "ShaderStruct.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace neurus {

// Forward declarations -- full includes only needed in .cpp
class Shader;
class ShaderCompiler;

/**
 * @brief Central shader registry that owns a ShaderCompiler internally.
 *
 * ShaderLibrary provides static Load*Shader() methods consumed by render
 * passes (GeometryPass, LightingPass, etc.) to obtain compiled shaders.
 * It owns a single ShaderCompiler instance for all GLSL&#8594;SPIR-V
 * compilation and caches compiled shaders by name.
 *
 * Architecture:
 *   - Owns ShaderCompiler internally (never exposed publicly).
 *   - Thread-safe cache via std::shared_mutex (concurrent reads, exclusive
 *     inserts).
 *   - DeferredRenderer never includes or knows about ShaderLibrary --
 *     passes call the static Load* methods directly.
 *   - Build-in constants (PI, Pix_UV_ratio, Gamma, FilmicF, FilmicV4)
 *     ported from the OpenGL ShaderBuildIn.cpp reference.
 *
 * @note RenderShader (Task 11) and ComputeShader (Task 12) are forward-
 *       declared -- the Load* methods return std::shared_ptr&lt;Shader&gt;
 *       base-class pointers.  Consumers downcast via std::static_pointer_cast.
 *       Placeholder implementations exist until Tasks 11/12 are complete.
 */
class ShaderLibrary
{
public:
	ShaderLibrary() = delete;
	~ShaderLibrary() = delete;
	ShaderLibrary(const ShaderLibrary&) = delete;
	ShaderLibrary& operator=(const ShaderLibrary&) = delete;

	// -------------------------------------------------------------------
	// Shader loading (public API for render passes)
	// -------------------------------------------------------------------

	/**
	 * @brief Loads or retrieves a cached RenderShader.
	 *
	 * On first call with a given name a RenderShader is created, compiled
	 * via the internal ShaderCompiler, and stored in the cache.  Subsequent
	 * calls return the cached shared_ptr.
	 *
	 * @param name     Unique shader name (cache key), e.g. "GeometryPass".
	 * @param vertPath Path to vertex shader GLSL source file.
	 * @param fragPath Path to fragment shader GLSL source file.
	 * @return Shared pointer to the compiled shader, or nullptr on failure
	 *         (currently a placeholder -- see Tasks 11/12).
	 */
	static std::shared_ptr<Shader> LoadRenderShader(
		const std::string& name,
		const std::string& vertPath,
		const std::string& fragPath);

	/**
	 * @brief Loads or retrieves a cached ComputeShader.
	 *
	 * On first call with a given name a ComputeShader is created, compiled
	 * via the internal ShaderCompiler, and stored in the cache.  Subsequent
	 * calls return the cached shared_ptr.
	 *
	 * @param name     Unique shader name (cache key), e.g. "SSAO".
	 * @param compPath Path to compute shader GLSL source file.
	 * @return Shared pointer to the compiled shader, or nullptr on failure
	 *         (currently a placeholder -- see Tasks 11/12).
	 */
	static std::shared_ptr<Shader> LoadComputeShader(
		const std::string& name,
		const std::string& compPath);

	// -------------------------------------------------------------------
	// Cache management
	// -------------------------------------------------------------------

	/**
	 * @brief Removes all cached shaders from the library.
	 *
	 * Useful for tests that need a clean shader state between cases.
	 * Takes an exclusive lock on the cache.
	 */
	static void Clear();

	/**
	 * @brief Forces recompilation of a cached shader on the next Load* call.
	 *
	 * Removes the shader entry from the cache by name.  The next
	 * LoadRenderShader() / LoadComputeShader() call with the same name
	 * will re-create and re-compile the shader from source.
	 *
	 * @param name Shader name to force-reload.
	 * @return true if the shader was found and removed, false if it was
	 *         not cached.
	 */
	static bool Reload(const std::string& name);

	// -------------------------------------------------------------------
	// Build-in constants (ported from OpenGL ShaderBuildIn.cpp)
	// -------------------------------------------------------------------

	/**
	 * @brief Looks up a build-in constant by name.
	 *
	 * Build-in constants are pre-defined S_Const values that can be
	 * injected into shader source during code generation.  The map is
	 * static and never modified at runtime.
	 *
	 * @param name Constant name, e.g. "B_PI", "B_PIX_UV_RATIO", "B_Gamma".
	 * @return Pointer to the S_Const, or nullptr if the name is not found.
	 */
	static const S_Const* GetBuildInConstant(const std::string& name);

private:
	// -------------------------------------------------------------------
	// Internal helpers (Meyer's singleton pattern)
	// -------------------------------------------------------------------

	/**
	 * @brief Thread-safe Get-or-Create for the shader cache.
	 *
	 * Uses double-checked locking: a shared_lock for the fast read path,
	 * then a unique_lock + second check for insertions.
	 *
	 * @param name    Cache key.
	 * @param factory Callable that creates a new shader on cache miss.
	 * @return Cached or newly-created shader (nullptr if factory fails).
	 */
	static std::shared_ptr<Shader> GetOrCreate(
		const std::string& name,
		const std::function<std::shared_ptr<Shader>()>& factory);
};

} // namespace neurus
