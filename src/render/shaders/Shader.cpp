/**
 * @file Shader.cpp
 * @brief Base-class implementations for Shader (constructor, GetShaderModule, TypeToString).
 */

#include "Shader.h"
#include "ShaderModule.h"

namespace neurus {

// ---------------------------------------------------------------------------
// Constructor -- initialises name and source; m_modules starts empty
// ---------------------------------------------------------------------------

Shader::Shader(std::string name, std::string source)
	: m_name(std::move(name))
	, m_source(std::move(source))
{
}

// ---------------------------------------------------------------------------
// Module lookup
// ---------------------------------------------------------------------------

std::shared_ptr<ShaderModule> Shader::GetShaderModule(ShaderType type) const
{
	auto it = m_modules.find(type);
	return (it != m_modules.end()) ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Type -> string conversion
// ---------------------------------------------------------------------------

std::string Shader::TypeToString(ShaderType type)
{
	switch (type)
	{
	case ShaderType::VERTEX:   return "VERTEX";
	case ShaderType::FRAGMENT: return "FRAGMENT";
	case ShaderType::COMPUTE:  return "COMPUTE";
	case ShaderType::GEOMETRY: return "GEOMETRY";
	default:                   return "UNKNOWN";
	}
}

} // namespace neurus
