#pragma once

#include "ShaderStruct.h"

#include <string>

namespace neurus {

struct ShaderUnit
{
	std::string path;         ///< Path to GLSL source file
	std::string code;         ///< Generated GLSL code (ShaderGenerator output)
	ShaderStruct parsed;      ///< Parsed IR (ShaderParser output)
};

} // namespace neurus
