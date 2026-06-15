/**
 * @file Parameters.h
 * @brief ParaType enum and GLSL type string table.
 *
 * Provides the ParaType enumeration for identifying shader parameter types
 * (float, vec2, mat4, sampler2D, etc.) and a static type_table that maps
 * each enum value to its corresponding GLSL type string.
 *
 * Architecture:
 * - Used by layers that need to describe shader uniform/parameter types
 * - Does NOT own values or provide storage (cf. OpenGL Parameters class)
 * - Pure type metadata — no runtime state
 *
 * @note ParaType is designed to match shader uniform types, not UI types.
 */

#pragma once

#include <string>
#include <vector>

namespace neurus {

/**
 * @brief Enumeration of supported shader parameter types.
 *
 * Maps 1:1 to GLSL shader uniform types. NONE_PARA indicates an
 * uninitialized or invalid parameter.
 */
enum ParaType
{
	NONE_PARA = -1, ///< Uninitialized or invalid parameter

	FLOAT,       ///< GLSL float
	VEC2,        ///< GLSL vec2
	VEC3,        ///< GLSL vec3
	VEC4,        ///< GLSL vec4
	MAT3,        ///< GLSL mat3
	MAT4,        ///< GLSL mat4
	INT,         ///< GLSL int
	BOOL,        ///< GLSL bool
	SAMPLER2D,   ///< GLSL sampler2D
	SAMPLERCUBE, ///< GLSL samplerCube
	IMAGE2D,     ///< GLSL image2D
	UINT         ///< GLSL uint
};

/**
 * @brief Maps ParaType to GLSL type string.
 *
 * Indexed by ParaType value (excluding NONE_PARA). Access via
 * type_table[FLOAT] == "float", type_table[MAT4] == "mat4", etc.
 */
inline const std::vector<const char*> type_table = {
	"float",      // FLOAT
	"vec2",       // VEC2
	"vec3",       // VEC3
	"vec4",       // VEC4
	"mat3",       // MAT3
	"mat4",       // MAT4
	"int",        // INT
	"bool",       // BOOL
	"sampler2D",  // SAMPLER2D
	"samplerCube", // SAMPLERCUBE
	"image2D",    // IMAGE2D
	"uint",       // UINT
};

} // namespace neurus
