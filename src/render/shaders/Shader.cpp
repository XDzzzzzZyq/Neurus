/**
 * @file Shader.cpp
 * @brief Base-class implementations for Shader (constructor, stage accessors, TypeToString).
 */

#include "Shader.h"

namespace neurus {

// --------------------------------------
// Constructor - initialises name only; m_stages starts empty
// --------------------------------------

Shader::Shader(std::string name)
	: m_name(std::move(name))
{
}

// --------------------------------------
// Stage accessors
// --------------------------------------

bool Shader::HasStage(ShaderType type) const
{
	return m_stages.count(type) > 0;
}

ShaderUnit& Shader::GetStage(ShaderType type)
{
	return m_stages[type];
}

const ShaderUnit& Shader::GetStage(ShaderType type) const
{
	return m_stages.at(type);
}

ShaderStruct& Shader::GetParsedStruct(ShaderType type)
{
	return m_stages[type].parsed;
}

const ShaderStruct& Shader::GetParsedStruct(ShaderType type) const
{
	return m_stages.at(type).parsed;
}

const std::string& Shader::GetGeneratedCode(ShaderType type) const
{
	return m_stages.at(type).code;
}

// --------------------------------------
// Type -> string conversion
// --------------------------------------

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
