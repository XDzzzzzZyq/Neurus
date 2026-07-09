/**
 * @file ShaderParser.h
 * @brief Vulkan-aware GLSL parser -- reads GLSL source line-by-line, classifies
 *        by keyword, and populates a ShaderStruct IR.
 *
 * Ported from OpenGL project's RenderShader::ParseShaderStream(), adapted for
 * Vulkan constructs (set/binding, push_constant, local_size, GLSL extensions,
 * multiview detection).
 *
 * Architecture:
 * - Static-only class -- ParseShaderCode() / ParseShaderFile() are the entry points.
 * - Populates ShaderStruct (Task 3) which is consumed by code generation (Task 8).
 * - Handles both render (vertex + fragment) and compute shaders.
 *
 * @note No OpenGL headers, no Qt, no Editor/UI dependencies.
 */

#pragma once

#include "ShaderStruct.h"
#include "Shader.h"

#include <string>

namespace neurus {

/**
 * @brief Stateless GLSL parser that populates a ShaderStruct IR from source.
 *
 * All methods are static.  The class exists purely as a namespace-like
 * grouping; it has no instance state.
 */
class ShaderParser
{
public:
	ShaderParser() = delete;
	~ShaderParser() = delete;

	/**
	 * @brief Reads a GLSL file from disk and parses it into a ShaderStruct.
	 *
	 * Opens `filepath`, reads the entire content into a string, and
	 * delegates to ParseShaderCode().
	 *
	 * @param filepath Path to the GLSL source file (.vert, .frag, .comp, .geom).
	 * @param type     Expected shader stage (VERTEX / FRAGMENT / COMPUTE / GEOMETRY).
	 * @param out      [out] ShaderStruct to populate.  Reset() is called first.
	 * @return true on successful parse, false if the file could not be opened
	 *         or if the source is unrecoverably malformed.
	 */
	static bool ParseShaderFile(const std::string& filepath, ShaderType type, ShaderStruct& out);

	/**
	 * @brief Parses a GLSL source string into a ShaderStruct IR.
	 *
	 * Reads the source line-by-line, classifying each line by keyword
	 * (#version, #extension, layout, in/out, uniform, struct, const,
	 * function definitions, bare variables) and calling the appropriate
	 * setter on `out`.
	 *
	 * @param source GLSL source text (null-terminated or newline-delimited).
	 * @param type   Shader stage -- used to disambiguate layout(input) vs layout(output) semantics.
	 * @param out    [out] ShaderStruct to populate.  Reset() is called first.
	 * @return true on successful parse (even if some lines were skipped),
	 *         false if a structural parse error was encountered.
	 */
	static bool ParseShaderCode(const std::string& source, ShaderType type, ShaderStruct& out);

private:
	// --- Internal helpers (defined in .cpp) ---

	/** @brief Strips single-line and block comments from a line of GLSL source. */
	static std::string StripComments(const std::string& line, bool& inBlockComment);

	/** @brief Trims leading and trailing whitespace. */
	static std::string TrimWhitespace(const std::string& s);

	/**
	 * @brief Extracts an integer value for a key from a layout(...) qualifier substring.
	 * @param layoutStr Content between "layout(" and ")" (e.g. "set = 0, binding = 5").
	 * @param key       Key to search for (e.g. "binding", "set", "location").
	 * @return The integer value, or -1 if not found.
	 */
	static int ExtractIntFromLayout(const std::string& layoutStr, const std::string& key);

	/** @brief Returns true if `layoutStr` contains the given keyword. */
	static bool HasLayoutKeyword(const std::string& layoutStr, const std::string& keyword);

	/**
	 * @brief Returns {size, alignment} for std140 layout of a GLSL type.
	 * @param typeName GLSL type string (e.g. "vec4", "mat4", "float").
	 * @return {sizeInBytes, alignmentInBytes}.  Returns {0, 0} for unknown types.
	 */
	static std::pair<uint32_t, uint32_t> GetStd140Layout(const std::string& typeName);
};

} // namespace neurus
