#include "ShaderLibrary.h"

#include "Shader.h"
#include "ShaderCompiler.h"

#include "RenderShader.h"
#include "ComputeShader.h"

#include "core/Log.h"

#include <mutex>
#include <shared_mutex>
#include <filesystem>

namespace neurus {

// =========================================================================
// Meyer's singleton - internal state via function-local statics
// =========================================================================

/**
 * @brief Returns the single ShaderCompiler instance owned by ShaderLibrary.
 *
 * Constructed on first call (thread-safe per C++11 magic statics).
 */
static ShaderCompiler& GetCompilerInstance()
{
	static ShaderCompiler s_compiler;
	return s_compiler;
}

/**
 * @brief Returns the thread-safe shader cache.
 */
static std::unordered_map<std::string, std::shared_ptr<Shader>>& GetCacheInstance()
{
	static std::unordered_map<std::string, std::shared_ptr<Shader>> s_cache;
	return s_cache;
}

/**
 * @brief Returns the shared mutex protecting the shader cache.
 */
static std::shared_mutex& GetMutexInstance()
{
	static std::shared_mutex s_mutex;
	return s_mutex;
}

// =========================================================================
// Build-in constants (ported from OpenGL ShaderBuildIn.cpp)
// =========================================================================

/**
 * @brief Returns the static map of build-in constants.
 *
 * Populated once on first call and never modified thereafter.
 * Contains mathematical constants (PI, Pix_UV_ratio) and colour-space
 * / tone-mapping functions (Gamma, FilmicF, FilmicV4).
 */
static const std::unordered_map<std::string, S_Const>& GetBuildInConstantsInstance()
{
	static const std::unordered_map<std::string, S_Const> s_constants = {
		// Mathematical constants
		{
			"B_PI",
			{
				ParaType::FLOAT,           // returnType
				"B_PI",                    // name
				"3.141592653589",          // body (GLSL expression)
				{}                          // args (empty = constant, not a function)
			}
		},
		{
			"B_PIX_UV_RATIO",
			{
				ParaType::FLOAT,
				"B_PIX_UV_RATIO",
				"1.0 / textureSize(select_texture, 0)",
				{}
			}
		},

		// Colour-space / tone-mapping functions (placeholders - bodies populated
		// by the shader code generator or configured at initialisation time)
		{
			"B_Gamma",
			{
				ParaType::VEC3,
				"B_Gamma",
				"",
				{}
			}
		},
		{
			"B_FilmicF",
			{
				ParaType::VEC3,
				"B_FilmicF",
				"",
				{}
			}
		},
		{
			"B_FilmicV4",
			{
				ParaType::VEC3,
				"B_FilmicV4",
				"",
				{}
			}
		},
	};
	return s_constants;
}

// =========================================================================
// File-static helpers
// =========================================================================

/**
 * @brief Resolves a potentially relative shader path to an accessible absolute path.
 *
 * Tries, in order:
 *   1. Already absolute -> return as-is
 *   2. Relative to NEURUS_SHADER_DIR (the compile-time cmake define)
 *   3. Multiple "../" prefixes (for GPU tests that run from build/debug/test/)
 *   4. Return the original path - let the caller fail with a clear error
 */
static std::string ResolveShaderPath(const std::string& path)
{
	// Already absolute? (Unix /, Windows \, or X: drive)
	if (!path.empty()
		&& (path[0] == '/' || path[0] == '\\'
			|| (path.size() > 1 && path[1] == ':')))
	{
		return path;
	}

	// Strip a leading "res/shaders/" prefix if present, so that paths like
	// "res/shaders/compute/foo.comp" resolve correctly against NEURUS_SHADER_DIR
	// (which already points to .../res/shaders/).  Without this, concatenation
	// produces a doubled ".../res/shaders/res/shaders/..." path.
	std::string relPath = path;
	{
		constexpr const char* kShaderPrefix = "res/shaders/";
		constexpr size_t kShaderPrefixLen = 12;
		if (relPath.size() > kShaderPrefixLen
			&& relPath.compare(0, kShaderPrefixLen, kShaderPrefix) == 0)
		{
			relPath.erase(0, kShaderPrefixLen);
		}
	}

	// Try relative to the compile-time shader directory
	{
		std::string resolved = NEURUS_SHADER_DIR + relPath;
		if (std::filesystem::exists(resolved))
		{
			NEURUS_LOG("[ShaderLibrary] Resolved '" << path << "' -> '" << resolved << "'");
			return resolved;
		}
	}

	// Fallback: walk up directories (tests run from build/debug/test/)
	for (const auto& prefix : {"../../../", "../../", "../", ""})
	{
		std::string resolved = std::string(prefix) + path;
		if (std::filesystem::exists(resolved))
		{
			NEURUS_LOG("[ShaderLibrary] Resolved '" << path << "' -> '" << resolved << "'");
			return resolved;
		}
	}

	// Could not resolve - return original and let the constructor fail with a clear error
	NEURUS_LOG("[ShaderLibrary] Could NOT resolve shader path '" << path << "'");
	return path;
}

// =========================================================================
// Public API
// =========================================================================

std::shared_ptr<RenderShader> ShaderLibrary::LoadRenderShader(
	const std::string& name,
	const std::string& vertPath,
	const std::string& fragPath)
{
	const std::string resolvedVert = ResolveShaderPath(vertPath);
	const std::string resolvedFrag = ResolveShaderPath(fragPath);

	auto shader = GetOrCreate(name, [&]() -> std::shared_ptr<Shader> {
		auto rs = std::make_shared<RenderShader>(name, resolvedVert, resolvedFrag);
		if (!rs->Compile(GetCompilerInstance()))
		{
			return nullptr;
		}
		return rs;
	});
	// Safe cast: LoadRenderShader only ever stores RenderShader instances.
	return std::static_pointer_cast<RenderShader>(shader);
}

std::shared_ptr<ComputeShader> ShaderLibrary::LoadComputeShader(
	const std::string& name,
	const std::string& compPath)
{
	const std::string resolvedComp = ResolveShaderPath(compPath);

	auto shader = GetOrCreate(name, [&]() -> std::shared_ptr<Shader> {
		auto cs = std::make_shared<ComputeShader>(name, resolvedComp);
		if (!cs->Compile(GetCompilerInstance()))
		{
			return nullptr;
		}
		return cs;
	});
	// Safe cast: LoadComputeShader only ever stores ComputeShader instances.
	return std::static_pointer_cast<ComputeShader>(shader);
}

void ShaderLibrary::Clear()
{
	std::unique_lock lock(GetMutexInstance());
	auto& cache = GetCacheInstance();
	cache.clear();

	NEURUS_LOG("[ShaderLibrary] Cache cleared (" << cache.size() << " entries)");
}

bool ShaderLibrary::Reload(const std::string& name)
{
	std::unique_lock lock(GetMutexInstance());
	auto& cache = GetCacheInstance();

	auto it = cache.find(name);
	if (it == cache.end())
	{
		return false;
	}

	cache.erase(it);
	NEURUS_LOG("[ShaderLibrary] Reloaded '" << name << "' - will recompile on next Load*");
	return true;
}

const S_Const* ShaderLibrary::GetBuildInConstant(const std::string& name)
{
	const auto& constants = GetBuildInConstantsInstance();
	auto it = constants.find(name);
	return (it != constants.end()) ? &it->second : nullptr;
}

// =========================================================================
// Private helpers
// =========================================================================

std::shared_ptr<Shader> ShaderLibrary::GetOrCreate(
	const std::string& name,
	const std::function<std::shared_ptr<Shader>()>& factory)
{
	// Fast path - shared lock for concurrent reads
	{
		std::shared_lock lock(GetMutexInstance());
		auto& cache = GetCacheInstance();
		auto it = cache.find(name);
		if (it != cache.end())
		{
			return it->second;
		}
	}

	// Slow path - exclusive lock for insertion
	std::unique_lock lock(GetMutexInstance());
	auto& cache = GetCacheInstance();

	// Double-check: another thread may have inserted while we waited
	// for the exclusive lock
	auto it = cache.find(name);
	if (it != cache.end())
	{
		return it->second;
	}

	// Create the shader (factory may be expensive - runs under exclusive lock,
	// but only on first access for each unique name)
	auto shader = factory();
	if (shader)
	{
		cache[name] = shader;
		NEURUS_LOG("[ShaderLibrary] Compiled and cached shader '" << name << "'");
	}
	else
	{
		NEURUS_ERR("[ShaderLibrary] Failed to compile shader '" << name << "'");
	}

	return shader;
}

} // namespace neurus
