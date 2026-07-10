/**
 * @file ComputeShader.cpp
 * @brief Implementation of the full parse->generate->compile pipeline for compute shaders.
 */

#include "ComputeShader.h"

#include "ShaderCompiler.h"
#include "ShaderModule.h"
#include "ShaderParser.h"

#include "core/Log.h"

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace neurus {

// =========================================================================
// Construction
// =========================================================================

ComputeShader::ComputeShader(const std::string& name, const std::string& compPath)
	: Shader(name, "")      // Source is populated during Compile
	, m_compPath(compPath)
{
	NEURUS_LOG("[ComputeShader] Created '" << name << "' from " << compPath);
}

// =========================================================================
// Shader interface
// =========================================================================

bool ComputeShader::Compile(ShaderCompiler& compiler)
{
	// -- Step 1: Read and parse the GLSL source file --
	m_struct.Reset();

	if (!ShaderParser::ParseShaderFile(m_compPath, ShaderType::COMPUTE, m_struct))
	{
		m_errorMessage = std::string("Failed to parse compute shader file: ") + m_compPath;
		NEURUS_ERR("[ComputeShader] " << m_errorMessage);
		return false;
	}

	// -- Step 2: Generate Vulkan GLSL from the IR --
	m_generatedSource = m_struct.GenerateShader();
	m_source = m_generatedSource; // Update base class source

	// -- Step 3: Compile GLSL to SPIR-V --
	m_spirv = compiler.CompileGlslToSpv(
		m_generatedSource,
		shaderc_glsl_compute_shader,
		"main",
		m_name);

	if (m_spirv.empty())
	{
		m_errorMessage = compiler.GetErrorMessage();
		NEURUS_ERR("[ComputeShader] SPIR-V compilation failed for '"
			<< m_name << "': " << m_errorMessage);
		return false;
	}

	NEURUS_LOG("[ComputeShader] '" << m_name
		<< "' compiled successfully (" << m_spirv.size() << " SPIR-V words, "
		<< "local_size=" << m_struct.local_size_x << "x"
		<< m_struct.local_size_y << "x" << m_struct.local_size_z << ")");

	return true;
}

bool ComputeShader::IsValid() const
{
	// Valid when SPIR-V bytecode is non-empty (compilation succeeded)
	return !m_spirv.empty();
}

// =========================================================================
// Compute-specific API
// =========================================================================

std::shared_ptr<ShaderModule> ComputeShader::GetModule()
{
	return Shader::GetShaderModule(ShaderType::COMPUTE);
}

bool ComputeShader::CreateModule(const vk::raii::Device& device)
{
	if (m_spirv.empty())
	{
		m_errorMessage = "Cannot create ShaderModule: SPIR-V is empty - call Compile() first";
		NEURUS_ERR("[ComputeShader] " << m_errorMessage);
		return false;
	}

	auto module = std::make_shared<ShaderModule>(device, m_spirv);
	m_modules[ShaderType::COMPUTE] = module;

	NEURUS_LOG("[ComputeShader] ShaderModule created for '" << m_name << "'");
	return true;
}

void ComputeShader::GetWorkgroupSize(uint32_t& x, uint32_t& y, uint32_t& z) const
{
	x = m_struct.local_size_x;
	y = m_struct.local_size_y;
	z = m_struct.local_size_z;
}

bool ComputeShader::Recompile(ShaderCompiler& compiler)
{
	// Re-generate GLSL from the current IR (may have been modified externally)
	m_generatedSource = m_struct.GenerateShader();
	m_source = m_generatedSource;

	// Re-compile to SPIR-V
	m_spirv = compiler.CompileGlslToSpv(
		m_generatedSource,
		shaderc_glsl_compute_shader,
		"main",
		m_name);

	if (m_spirv.empty())
	{
		m_errorMessage = compiler.GetErrorMessage();
		NEURUS_ERR("[ComputeShader] Recompilation failed for '"
			<< m_name << "': " << m_errorMessage);
		return false;
	}

	// If a module was already created, it's now stale - clear it.
	// The caller must call CreateModule() again to recreate it.
	m_modules.erase(ShaderType::COMPUTE);

	NEURUS_LOG("[ComputeShader] '" << m_name << "' recompiled ("
		<< m_spirv.size() << " SPIR-V words)");

	return true;
}

// =========================================================================
// Default uniform configurations (ported from OpenGL ComputeShader)
// =========================================================================

void ComputeShader::SetDefault(const std::string& name, int value)
{
	// Remove existing entry with the same name, then append (overwrite semantics)
	m_defaults.erase(
		std::remove_if(m_defaults.begin(), m_defaults.end(),
			[&](const Default& d) { return d.name == name; }),
		m_defaults.end());
	m_defaults.push_back({name, "int", std::to_string(value)});
}

void ComputeShader::SetDefault(const std::string& name, float value)
{
	m_defaults.erase(
		std::remove_if(m_defaults.begin(), m_defaults.end(),
			[&](const Default& d) { return d.name == name; }),
		m_defaults.end());
	m_defaults.push_back({name, "float", std::to_string(value)});
}

void ComputeShader::SetDefault(const std::string& name, bool value)
{
	m_defaults.erase(
		std::remove_if(m_defaults.begin(), m_defaults.end(),
			[&](const Default& d) { return d.name == name; }),
		m_defaults.end());
	m_defaults.push_back({name, "bool", value ? "true" : "false"});
}

} // namespace neurus
