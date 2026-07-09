/**
 * @file RenderShader.cpp
 * @brief Implementation of RenderShader -- vertex+fragment shader compilation pipeline.
 *
 * Implements the full parse->generate->compile flow ported from the OpenGL
 * RenderShader, adapted for Vulkan via ShaderParser, ShaderStruct::GenerateShader(),
 * ShaderCompiler::CompileGlslToSpv(), and ShaderModule.
 */

#include "RenderShader.h"

#include "ShaderCompiler.h"
#include "ShaderParser.h"
#include "core/Log.h"

#include <shaderc/shaderc.hpp>

namespace neurus {

// File-static helper -- keeps shaderc dependency out of the header
namespace {

shaderc_shader_kind ToShadercKind(ShaderType type)
{
	switch (type)
	{
	case ShaderType::VERTEX:   return shaderc_glsl_vertex_shader;
	case ShaderType::FRAGMENT: return shaderc_glsl_fragment_shader;
	case ShaderType::COMPUTE:  return shaderc_glsl_compute_shader;
	case ShaderType::GEOMETRY: return shaderc_glsl_geometry_shader;
	default:                   return shaderc_glsl_vertex_shader; // fallback
	}
}

} // anonymous namespace

// =========================================================================
// Constructor
// =========================================================================

RenderShader::RenderShader(const std::string& name,
                           const std::string& vertPath,
                           const std::string& fragPath)
	: Shader(name, /* source generated later */ "")
	, m_vertPath(vertPath)
	, m_fragPath(fragPath)
{
	NEURUS_LOG("[RenderShader] Created '" << name
		<< "' vert='" << vertPath << "' frag='" << fragPath << "'");
}

// =========================================================================
// Shader interface -- Compile
// =========================================================================

bool RenderShader::Compile(ShaderCompiler& compiler)
{
	NEURUS_LOG("[RenderShader] Compiling '" << m_name << "'...");

	// Reset state before compilation
	m_vertSpirv.clear();
	m_fragSpirv.clear();
	m_modules.clear();
	m_errorMessage.clear();

	// --- Compile vertex stage ---
	if (!CompileStage(compiler, ShaderType::VERTEX, m_vertPath, m_vertStruct, m_vertSpirv))
	{
		return false;
	}

	// --- Compile fragment stage ---
	if (!CompileStage(compiler, ShaderType::FRAGMENT, m_fragPath, m_fragStruct, m_fragSpirv))
	{
		return false;
	}

	// --- Create ShaderModules if device is already set ---
	if (m_device)
	{
		auto vertMod = CreateModuleFromSpirv(m_vertSpirv);
		auto fragMod = CreateModuleFromSpirv(m_fragSpirv);

		if (vertMod && fragMod)
		{
			m_modules[ShaderType::VERTEX]   = std::move(vertMod);
			m_modules[ShaderType::FRAGMENT] = std::move(fragMod);
		}
		else
		{
			NEURUS_ERR("[RenderShader] Failed to create ShaderModules for '" << m_name << "'");
			return false;
		}
	}

	NEURUS_LOG("[RenderShader] '" << m_name
		<< "' compiled successfully"
		<< " (vert SPIR-V: " << m_vertSpirv.size() << " words"
		<< ", frag SPIR-V: " << m_fragSpirv.size() << " words)");

	return true;
}

// =========================================================================
// Shader interface -- IsValid / GetType
// =========================================================================

bool RenderShader::IsValid() const
{
	// Valid if both SPIR-V stages compiled successfully
	return !m_vertSpirv.empty() && !m_fragSpirv.empty();
}

// =========================================================================
// RenderShader-specific -- GetStruct
// =========================================================================

ShaderStruct& RenderShader::GetStruct(ShaderType type)
{
	if (type == ShaderType::FRAGMENT)
	{
		return m_fragStruct;
	}
	// Default: return vertex struct (also handles unrecognised types)
	return m_vertStruct;
}

// =========================================================================
// RenderShader-specific -- Module access
// =========================================================================

std::shared_ptr<ShaderModule> RenderShader::GetVertexModule()
{
	// Lazy-create from SPIR-V if device is set but module not yet created
	auto it = m_modules.find(ShaderType::VERTEX);
	if (it != m_modules.end())
	{
		return it->second;
	}

	if (m_device && !m_vertSpirv.empty())
	{
		auto mod = CreateModuleFromSpirv(m_vertSpirv);
		if (mod)
		{
			m_modules[ShaderType::VERTEX] = mod;
			return mod;
		}
	}

	return nullptr;
}

std::shared_ptr<ShaderModule> RenderShader::GetFragmentModule()
{
	auto it = m_modules.find(ShaderType::FRAGMENT);
	if (it != m_modules.end())
	{
		return it->second;
	}

	if (m_device && !m_fragSpirv.empty())
	{
		auto mod = CreateModuleFromSpirv(m_fragSpirv);
		if (mod)
		{
			m_modules[ShaderType::FRAGMENT] = mod;
			return mod;
		}
	}

	return nullptr;
}

// =========================================================================
// RenderShader-specific -- Recompile
// =========================================================================

bool RenderShader::Recompile(ShaderCompiler& compiler, ShaderType type)
{
	if (type != ShaderType::VERTEX && type != ShaderType::FRAGMENT)
	{
		NEURUS_ERR("[RenderShader] Recompile '" << m_name
			<< "': unsupported shader type " << Shader::TypeToString(type));
		return false;
	}

	NEURUS_LOG("[RenderShader] Recompiling '"
		<< m_name << "' stage " << Shader::TypeToString(type) << "...");

	// Select the right stage data
	ShaderStruct&    shaderStruct = (type == ShaderType::FRAGMENT) ? m_fragStruct : m_vertStruct;
	std::string&     filepath     = (type == ShaderType::FRAGMENT) ? m_fragPath  : m_vertPath;
	std::vector<uint32_t>& spirv = (type == ShaderType::FRAGMENT) ? m_fragSpirv : m_vertSpirv;

	// --- Re-generate GLSL if the struct has changed ---
	if (shaderStruct.is_struct_changed)
	{
		shaderStruct.GenerateShader();
		// is_struct_changed is now false (cleared by GenerateShader)
	}

	// --- Re-parse from file (always re-read for live editing) ---
	if (!ShaderParser::ParseShaderFile(filepath, type, shaderStruct))
	{
		m_errorMessage = "Failed to parse shader file: " + filepath;
		NEURUS_ERR("[RenderShader] " << m_errorMessage);
		return false;
	}

	// --- Generate GLSL ---
	std::string glsl = shaderStruct.GenerateShader();

	// --- Compile GLSL to SPIR-V ---
	spirv = compiler.CompileGlslToSpv(
		glsl,
		ToShadercKind(type),
		"main",
		m_name + "_" + Shader::TypeToString(type));

	if (spirv.empty())
	{
		m_errorMessage = "SPIR-V compilation failed for stage "
			+ Shader::TypeToString(type) + ": " + compiler.GetErrorMessage();
		NEURUS_ERR("[RenderShader] " << m_errorMessage);
		return false;
	}

	// --- Swap ShaderModule if device is set ---
	if (m_device)
	{
		auto newMod = CreateModuleFromSpirv(spirv);
		if (newMod)
		{
			m_modules[type] = std::move(newMod);
		}
		else
		{
			NEURUS_ERR("[RenderShader] Recompile '" << m_name
				<< "': failed to create ShaderModule for stage "
				<< Shader::TypeToString(type));
			return false;
		}
	}

	NEURUS_LOG("[RenderShader] Recompile '" << m_name
		<< "' stage " << Shader::TypeToString(type)
		<< " OK (" << spirv.size() << " SPIR-V words)");

	return true;
}

// =========================================================================
// SetDevice
// =========================================================================

void RenderShader::SetDevice(const vk::raii::Device& device)
{
	m_device = &device;
	NEURUS_LOG("[RenderShader] Device set for '" << m_name << "'");

	// If SPIR-V already compiled but modules not yet created, create them now
	if (!m_vertSpirv.empty() && !m_fragSpirv.empty())
	{
		auto vertMod = CreateModuleFromSpirv(m_vertSpirv);
		auto fragMod = CreateModuleFromSpirv(m_fragSpirv);

		if (vertMod && fragMod)
		{
			m_modules[ShaderType::VERTEX]   = std::move(vertMod);
			m_modules[ShaderType::FRAGMENT] = std::move(fragMod);
			NEURUS_LOG("[RenderShader] Modules created for '" << m_name << "'");
		}
		else
		{
			NEURUS_ERR("[RenderShader] Failed to create modules for '" << m_name << "'");
		}
	}
}

// =========================================================================
// Private helpers
// =========================================================================

bool RenderShader::CompileStage(ShaderCompiler& compiler,
                                ShaderType type,
                                const std::string& filepath,
                                ShaderStruct& shaderStruct,
                                std::vector<uint32_t>& outSpirv)
{
	const std::string stageName = Shader::TypeToString(type);

	// --- 1. Parse GLSL source file into ShaderStruct IR ---
	if (!ShaderParser::ParseShaderFile(filepath, type, shaderStruct))
	{
		m_errorMessage = "Failed to parse shader file: " + filepath;
		NEURUS_ERR("[RenderShader] " << m_errorMessage);
		return false;
	}

	// --- 2. Generate Vulkan GLSL from the IR ---
	std::string glsl = shaderStruct.GenerateShader();

	if (glsl.empty())
	{
		m_errorMessage = "Generated empty GLSL for " + stageName + " stage";
		NEURUS_ERR("[RenderShader] " << m_errorMessage);
		return false;
	}

	// --- 3. Compile GLSL to SPIR-V via shaderc ---
	outSpirv = compiler.CompileGlslToSpv(
		glsl,
		ToShadercKind(type),
		"main",
		m_name + "_" + stageName);

	if (outSpirv.empty())
	{
		m_errorMessage = "SPIR-V compilation failed for " + stageName
			+ " stage: " + compiler.GetErrorMessage();
		NEURUS_ERR("[RenderShader] " << m_errorMessage);
		return false;
	}

	NEURUS_LOG("[RenderShader] " << stageName << " stage compiled: "
		<< outSpirv.size() << " SPIR-V words");

	return true;
}

std::shared_ptr<ShaderModule> RenderShader::CreateModuleFromSpirv(const std::vector<uint32_t>& spirv)
{
	if (!m_device || spirv.empty())
	{
		return nullptr;
	}

	try
	{
		return std::make_shared<ShaderModule>(*m_device, spirv);
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("[RenderShader] ShaderModule creation failed: " << e.what());
		return nullptr;
	}
}

} // namespace neurus
