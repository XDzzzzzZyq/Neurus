#pragma once

#include "ShaderStruct.h"

#include <string>

namespace neurus {

struct ShaderUnit
{
	std::string path;         ///< Path to GLSL source file
	std::string code;         ///< Generated GLSL code (ShaderGenerator output)
	ShaderStruct parsed;      ///< Parsed IR (ShaderParser output)
	int         m_version = 0;  ///< Monotonic version counter; bumped on create/edit/compile

	int  GetVersion() const   { return m_version; }
	void BumpVersion()         { m_version++; }
};

} // namespace neurus
