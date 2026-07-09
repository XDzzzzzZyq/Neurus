/**
 * @file ShaderParser.cpp
 * @brief Implementation of the Vulkan-aware GLSL parser.
 *
 * Ported from OpenGL project's RenderShader::ParseShaderStream(),
 * extended with Vulkan-specific constructs (set/binding, push_constant,
 * local_size, GLSL extensions, multiview).
 */

#include "ShaderParser.h"

#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace neurus {

// ============================================================================
// Internal helper functions
// ============================================================================

std::string ShaderParser::StripComments(const std::string& line, bool& inBlockComment)
{
	std::string result;
	result.reserve(line.size());

	for (size_t i = 0; i < line.size(); ++i)
	{
		if (inBlockComment)
		{
			if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/')
			{
				inBlockComment = false;
				++i; // skip '/'
			}
		}
		else
		{
			if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')
			{
				// Line comment ---discard rest of line.
				break;
			}
			if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '*')
			{
				inBlockComment = true;
				++i; // skip '*'
			}
			else
			{
				result += line[i];
			}
		}
	}

	return result;
}

std::string ShaderParser::TrimWhitespace(const std::string& s)
{
	size_t start = 0;
	while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
	{
		++start;
	}

	size_t end = s.size();
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
	{
		--end;
	}

	return s.substr(start, end - start);
}

int ShaderParser::ExtractIntFromLayout(const std::string& layoutStr, const std::string& key)
{
	size_t pos = 0;
	while (pos < layoutStr.size())
	{
		// Find next occurrence of the key
		pos = layoutStr.find(key, pos);
		if (pos == std::string::npos)
		{
			return -1;
		}

		// Advance past the key
		pos += key.size();

		// Skip whitespace and '='
		while (pos < layoutStr.size() && (layoutStr[pos] == ' ' || layoutStr[pos] == '\t' || layoutStr[pos] == '='))
		{
			++pos;
		}

		// Read the number
		if (pos < layoutStr.size() && std::isdigit(static_cast<unsigned char>(layoutStr[pos])))
		{
			std::string num;
			while (pos < layoutStr.size() && std::isdigit(static_cast<unsigned char>(layoutStr[pos])))
			{
				num += layoutStr[pos++];
			}
			return std::stoi(num);
		}

		// Not a match (e.g. key is a substring of another word) ---keep looking
	}

	return -1;
}

bool ShaderParser::HasLayoutKeyword(const std::string& layoutStr, const std::string& keyword)
{
	size_t pos = layoutStr.find(keyword);
	if (pos == std::string::npos)
	{
		return false;
	}

	// Ensure the keyword is a standalone token (not part of another word).
	// Check character before: must be start, whitespace, or comma.
	if (pos > 0)
	{
		char before = layoutStr[pos - 1];
		if (before != ' ' && before != '\t' && before != ',' && before != '(')
		{
			return false;
		}
	}

	// Check character after: must be end, whitespace, or comma.
	size_t after = pos + keyword.size();
	if (after < layoutStr.size())
	{
		char next = layoutStr[after];
		if (next != ' ' && next != '\t' && next != ',' && next != ')' && next != ')')
		{
			return false;
		}
	}

	return true;
}

std::pair<uint32_t, uint32_t> ShaderParser::GetStd140Layout(const std::string& typeName)
{
	// Scalars ---4 bytes, aligned to 4
	if (typeName == "float" || typeName == "int" || typeName == "uint" || typeName == "bool")
	{
		return {4, 4};
	}

	// 2-component vectors ---8 bytes, aligned to 8
	if (typeName == "vec2" || typeName == "ivec2" || typeName == "uvec2")
	{
		return {8, 8};
	}

	// 3-component vectors ---std140 pads to vec4 alignment (16)
	if (typeName == "vec3" || typeName == "ivec3" || typeName == "uvec3")
	{
		return {12, 16};
	}

	// 4-component vectors ---16 bytes
	if (typeName == "vec4" || typeName == "ivec4" || typeName == "uvec4")
	{
		return {16, 16};
	}

	// Matrices ---column vectors of vec4
	// mat3: 3 vec4 columns = 48 bytes
	if (typeName == "mat3")
	{
		return {48, 16};
	}

	// mat4: 4 vec4 columns = 64 bytes
	if (typeName == "mat4")
	{
		return {64, 16};
	}

	// mat4x3: 3 vec4 columns (std140 treats matMxN as M columns of vecN, padded)
	if (typeName == "mat4x3")
	{
		return {48, 16};
	}

	// Unknown ---caller must handle
	return {0, 0};
}

// ============================================================================
// ParseShaderFile
// ============================================================================

bool ShaderParser::ParseShaderFile(const std::string& filepath, ShaderType type, ShaderStruct& out)
{
	std::ifstream file(filepath);
	if (!file.is_open())
	{
		NEURUS_ERR("ShaderParser: cannot open file '" << filepath << "'");
		return false;
	}

	std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	if (source.empty())
	{
		NEURUS_ERR("ShaderParser: file is empty '" << filepath << "'");
		return false;
	}

	return ParseShaderCode(source, type, out);
}

// ============================================================================
// ParseShaderCode ---main line-by-line parser
// ============================================================================

bool ShaderParser::ParseShaderCode(const std::string& source, ShaderType /*type*/, ShaderStruct& out)
{
	out.Reset();

	std::istringstream stream(source);
	std::string line;
	std::string word;      // Temporary word buffer reused across parsing branches
	std::string cache;     // Accumulates function body text
	Args argsCache;        // Accumulates (type, name) pairs for block/struct members
	bool inBlockComment = false;
	bool parsed = false; // Set to true when any recognised construct is processed

	while (std::getline(stream, line))
	{
		// --- Strip comments and whitespace ---
		line = StripComments(line, inBlockComment);
		line = TrimWhitespace(line);

		if (line.empty())
		{
			continue;
		}

		// ---------------------------------------------------------------
		// #version ---Vulkan GLSL requires #version 450 minimum
		// ---------------------------------------------------------------
		if (line.compare(0, 8, "#version") == 0)
		{
			std::istringstream iss(line);
			std::string token;
			iss >> token; // "#version"
			iss >> token; // number
			int ver = std::atoi(token.c_str());
			if (ver > 0)
			{
				out.SetVersion(ver);
				parsed = true;
			}
			continue;
		}

		// ---------------------------------------------------------------
		// #extension NAME : require/enable/warn/disable
		// ---------------------------------------------------------------
		if (line.compare(0, 10, "#extension") == 0)
		{
			std::istringstream iss(line);
			std::string token;
			iss >> token; // "#extension"
			iss >> token; // extension name (e.g. "GL_EXT_multiview")
			out.AddExtension(token);
			continue;
		}

		// ---------------------------------------------------------------
		// layout(...) ---the big one.  Handle compute local_size,
		// push_constant blocks, uniform blocks, storage buffers,
		// vertex/fragment inputs/outputs, standalone sampler uniforms.
		// ---------------------------------------------------------------
		if (line.find("layout") == 0)
		{
			// Extract the layout qualifier substring between "layout(" and ")"
			size_t parenOpen = line.find('(');
			size_t parenClose = line.find(')');

			if (parenOpen == std::string::npos || parenClose == std::string::npos)
			{
				NEURUS_ERR("ShaderParser: malformed layout qualifier on line: " << line);
				return false;
			}

			std::string layoutStr = line.substr(parenOpen + 1, parenClose - parenOpen - 1);
			std::string afterLayout = TrimWhitespace(line.substr(parenClose + 1));

			// --- Compute shader local_size: layout(local_size_x=X, ...) in; ---
			if (afterLayout.find("in;") != std::string::npos)
			{
				int lx = ExtractIntFromLayout(layoutStr, "local_size_x");
				int ly = ExtractIntFromLayout(layoutStr, "local_size_y");
				int lz = ExtractIntFromLayout(layoutStr, "local_size_z");

				if (lx > 0)
				{
					out.SetLocalSize(
						static_cast<uint32_t>(lx),
						static_cast<uint32_t>(ly > 0 ? ly : 1),
						static_cast<uint32_t>(lz > 0 ? lz : 1));
				}
				continue;
			}

			// --- Storage buffer: layout(...) buffer BlockName { ... } varName; ---
			if (afterLayout.find("buffer") == 0 || (afterLayout.find("readonly") == 0 && afterLayout.find("buffer") != std::string::npos))
			{
				// Move past buffer/readonly keyword
				std::istringstream iss(afterLayout);
				std::string word;

				// Skip qualifiers (readonly, writeonly, buffer)
				while (iss >> word)
				{
					if (word == "buffer")
					{
						break;
					}
				}

				// Read block type name
				iss >> word;
				std::string blockName = word;

				// Read variable name from the rest (if present after block)
				std::string varName;
				iss >> varName;

				// Read lines until closing "};" to collect block members
				argsCache.clear();
				bool foundOpenBrace = (line.find('{') != std::string::npos);

			do
			{
				if (!foundOpenBrace)
				{
					if (!std::getline(stream, line))
					{
						NEURUS_ERR("ShaderParser: unexpected EOF while parsing storage buffer block");
						return false;
					}
					line = StripComments(line, inBlockComment);
					line = TrimWhitespace(line);
					if (line == "{")
					{
						foundOpenBrace = true;
						continue;
					}
				}

				if (!std::getline(stream, line))
				{
					NEURUS_ERR("ShaderParser: unexpected EOF while parsing storage buffer members");
					return false;
				}
				line = StripComments(line, inBlockComment);
				line = TrimWhitespace(line);

			if (line.find('}') != std::string::npos && line.find(';') != std::string::npos)
			{
				break;
			}

			if (line.empty() || line == "{" || line == "}")
			{
				continue;
			}

			// Parse member: "type name;" or "type name[SIZE];"
			std::istringstream mstr(line);
			std::string typeName;
			mstr >> typeName;
			mstr >> word;

			// Strip trailing semicolon
			if (!word.empty() && word.back() == ';')
			{
				word.pop_back();
			}

			ParaType pType = ShaderStruct::ParseType(typeName);
			argsCache.emplace_back(pType, word, typeName);
		}
		while (true);

				int binding = ExtractIntFromLayout(layoutStr, "binding");
				if (binding < 0)
				{
					binding = 0;
				}

				out.SetSB(binding, blockName, argsCache);
				argsCache.clear();
				continue;
			}

			// --- Push constant block: layout(push_constant) uniform BlockName { ... } varName; ---
			if (HasLayoutKeyword(layoutStr, "push_constant"))
			{
				// Read until opening brace
				bool foundOpenBrace = (afterLayout.find('{') != std::string::npos);

			while (!foundOpenBrace)
			{
				if (!std::getline(stream, line))
				{
					NEURUS_ERR("ShaderParser: unexpected EOF while parsing push constant block");
					return false;
				}
				line = StripComments(line, inBlockComment);
				line = TrimWhitespace(line);
				if (line.empty())
				{
					continue;
				}
				if (line.find('{') != std::string::npos)
				{
					foundOpenBrace = true;
				}
			}

				// Read block members ---track std140 offsets
				uint32_t currentOffset = 0;

			do
			{
				if (!std::getline(stream, line))
				{
					NEURUS_ERR("ShaderParser: unexpected EOF while parsing push constant members");
					return false;
				}
				line = StripComments(line, inBlockComment);
				line = TrimWhitespace(line);

			if (line.find('}') != std::string::npos && line.find(';') != std::string::npos)
			{
				break;
			}

			if (line.empty() || line == "{" || line == "}")
			{
				continue;
			}

			// Handle explicit layout(offset=N) inside push constant blocks
				uint32_t explicitOffset = UINT32_MAX;
				if (line.find("layout") == 0 && line.find("offset") != std::string::npos)
					{
						size_t oOpen = line.find('(');
						size_t oClose = line.find(')');
						if (oOpen != std::string::npos && oClose != std::string::npos)
						{
							std::string offsetLayout = line.substr(oOpen + 1, oClose - oOpen - 1);
							int off = ExtractIntFromLayout(offsetLayout, "offset");
							if (off >= 0)
							{
								explicitOffset = static_cast<uint32_t>(off);
							}
						}
						line = TrimWhitespace(line.substr(oClose + 1));
					}

					// Parse member: "type name;"
					std::istringstream mstr(line);
					std::string typeName;
					mstr >> typeName;
					std::string word;
					mstr >> word;

					// Strip trailing semicolon
					if (!word.empty() && word.back() == ';')
					{
						word.pop_back();
					}

					auto [size, align] = GetStd140Layout(typeName);

					if (size == 0 && align == 0)
					{
						// Unknown type ---try treating all non-trivial types as 16-byte aligned
						if (!typeName.empty())
						{
							size = 4;
							align = 4;
						}
					}

					if (explicitOffset != UINT32_MAX)
					{
						currentOffset = explicitOffset;
					}
					else
					{
						// Align current offset
						if (align > 0 && (currentOffset % align) != 0)
						{
							currentOffset = ((currentOffset / align) + 1) * align;
						}
					}

					out.SetPushConstant(word, currentOffset, size, typeName);
					currentOffset += size;
				}
			while (true);

			// Extract variable name from closing line (e.g. "} pc;" or "};")
			{
				size_t closeBrace = line.find('}');
				size_t semi        = line.find(';');
				if (closeBrace != std::string::npos && semi != std::string::npos
				    && semi > closeBrace + 1)
				{
					out.SetPushConstantVar(
						TrimWhitespace(line.substr(closeBrace + 1, semi - closeBrace - 1)));
				}
			}

			continue;
		}

		// --- Uniform block: layout(...) uniform BlockType { ... } varName; ---
		// Distinguish from standalone uniforms by checking for '{' (same/next line)
		// or absence of ';' (standalone uniforms have ';' on the same line).
		if (afterLayout.find("uniform") != std::string::npos
		    && (afterLayout.find('{') != std::string::npos
		        || afterLayout.find(';') == std::string::npos))
		{
			// Find the actual block type name from afterLayout or the next line,
			// then look for '{' which may be on this line or a subsequent line.
			size_t uniPos = afterLayout.find("uniform ");
			std::string blockDecl = afterLayout.substr(uniPos + 8);
			std::istringstream iss(blockDecl);
			std::string blockType;
			iss >> blockType;

			// Look for opening brace (may be on this line or a later line)
			bool foundOpenBrace = (afterLayout.find('{') != std::string::npos);

			while (!foundOpenBrace)
			{
				if (!std::getline(stream, line))
				{
					NEURUS_ERR("ShaderParser: unexpected EOF while parsing uniform block");
					return false;
				}
				line = StripComments(line, inBlockComment);
				line = TrimWhitespace(line);
				if (line.empty())
				{
					continue;
				}
				if (line.find('{') != std::string::npos)
				{
					foundOpenBrace = true;
				}
				else
				{
					// It might be the block type name on its own line
					blockType = line;
				}
			}

			// Read members until "};"
			argsCache.clear();

		do
		{
			if (!std::getline(stream, line))
			{
				NEURUS_ERR("ShaderParser: unexpected EOF while parsing uniform block members");
				return false;
			}
			line = StripComments(line, inBlockComment);
			line = TrimWhitespace(line);

		if (line.find('}') != std::string::npos && line.find(';') != std::string::npos)
		{
			break;
		}

		if (line.empty() || line == "{" || line == "}")
		{
			continue;
		}

		// Parse member: "type name;"
		std::istringstream mstr(line);
		std::string typeName;
		mstr >> typeName;
		mstr >> word;

		if (!word.empty() && word.back() == ';')
		{
			word.pop_back();
		}

		ParaType pType = ShaderStruct::ParseType(typeName);
		argsCache.emplace_back(pType, word, typeName);
	}
	while (true);

		// Extract variable name from closing line (e.g. "} camera;" or "};")
		std::string ubVarName;
		{
			size_t closeBrace = line.find('}');
			size_t semi        = line.find(';');
			if (closeBrace != std::string::npos && semi != std::string::npos
			    && semi > closeBrace + 1)
			{
				ubVarName = TrimWhitespace(
					line.substr(closeBrace + 1, semi - closeBrace - 1));
			}
		}

		out.SetUB(blockType, ubVarName, argsCache,
		          ExtractIntFromLayout(layoutStr, "binding"));
		argsCache.clear();
		continue;
	}

		// --- Standalone uniform with layout: layout(binding=N) uniform type name; ---
		if (afterLayout.find("uniform") != std::string::npos && afterLayout.find('{') == std::string::npos)
		{
			// Capture qualifiers ("writeonly", "readonly") before stripping
			std::string qualifiers;
			if (afterLayout.find("writeonly") != std::string::npos)
			{
				qualifiers = "writeonly";
			}
			else if (afterLayout.find("readonly") != std::string::npos)
			{
				qualifiers = "readonly";
			}

			// Strip qualifiers like "writeonly", "readonly", "uniform"
			std::string decl = afterLayout;
			for (const auto& qualifier : {"writeonly", "readonly", "uniform"})
			{
				size_t qpos = decl.find(qualifier);
				if (qpos != std::string::npos)
				{
					decl = decl.substr(qpos + std::strlen(qualifier));
					break;
				}
			}
			decl = TrimWhitespace(decl);

			std::istringstream iss(decl);
			std::string typeName;
				iss >> typeName;
				iss >> word;

				// Strip trailing semicolon
				if (!word.empty() && word.back() == ';')
				{
					word.pop_back();
				}

		ParaType pType = ShaderStruct::ParseType(typeName);
			int binding = ExtractIntFromLayout(layoutStr, "binding");
			// Pass actualType for custom types like "image2D" that ParseType may lose
			std::string actualType;
			if (pType == ParaType::CUSTOM || !qualifiers.empty())
			{
				actualType = typeName;
			}
			out.SetUni(pType, 1, word, binding, qualifiers, actualType);
			continue;
			}

			// --- layout(location=N) in type name; ???vertex attributes / fragment inputs ---
			if (afterLayout.find("in ") != std::string::npos && afterLayout.find("in;") == std::string::npos)
			{
				int location = ExtractIntFromLayout(layoutStr, "location");
				if (location < 0)
				{
					location = 0;
				}

				// Skip "in" keyword
				size_t inPos = afterLayout.find("in ");
				std::string decl = afterLayout.substr(inPos + 3);
				std::istringstream iss(decl);
				std::string typeName;
				iss >> typeName;
				iss >> word;

				if (!word.empty() && word.back() == ';')
				{
					word.pop_back();
				}

				ParaType pType = ShaderStruct::ParseType(typeName);
				out.SetAB(location, pType, word);
				continue;
			}

			// --- layout(location=N) out type name; ???render-pass outputs ---
			if (afterLayout.find("out ") != std::string::npos)
			{
				int location = ExtractIntFromLayout(layoutStr, "location");
				if (location < 0)
				{
					location = 0;
				}

				// Skip "out" keyword
				size_t outPos = afterLayout.find("out ");
				std::string decl = afterLayout.substr(outPos + 4);
				std::istringstream iss(decl);
				std::string typeName;
				iss >> typeName;
				iss >> word;

				if (!word.empty() && word.back() == ';')
				{
					word.pop_back();
				}

				ParaType pType = ShaderStruct::ParseType(typeName);
				out.SetPass(location, pType, word);
				continue;
			}

			// Unrecognised layout qualifier ---skip
			continue;
		} // end layout(...)

		// ---------------------------------------------------------------
		// Stacked lines (multi-line declarations) ---merge with cache
		// ---------------------------------------------------------------
		std::string fullLine = line;

		// If the line ends with '{' but not with '}', read the full block
		if (line.find('{') != std::string::npos && line.find('}') == std::string::npos)
		{
			// This is the start of a block that spans multiple lines.
			// We'll handle it in the branches below.
		}

		// ---------------------------------------------------------------
		// Bare "out type name;" ???inter-stage output (auto-link)
		// ---------------------------------------------------------------
		if (fullLine.find("out ") != std::string::npos && fullLine.find("layout") == std::string::npos)
		{
			std::istringstream iss(fullLine);
			std::string word;
			iss >> word; // "out"
			iss >> word; // type

			// Skip qualifier keywords like "vec3", "vec4", etc. if they appear
			ParaType paraType = ShaderStruct::ParseType(word);
			iss >> word; // name

			if (!word.empty() && word.back() == ';')
			{
				word.pop_back();
			}

			out.SetOut(paraType, 1, word);
			continue;
		}

		// ---------------------------------------------------------------
		// Bare "in type name;" ???inter-stage input
		// ---------------------------------------------------------------
		if (fullLine.find("in ") != std::string::npos && fullLine.find("layout") == std::string::npos)
		{
			// In the OpenGL parser, bare "in" is skipped because inputs are
			// auto-linked from the paired shader's outputs.  We parse them
			// here for completeness.
			std::istringstream iss(fullLine);
			std::string word;
			iss >> word; // "in"
			iss >> word; // type

			ParaType paraType = ShaderStruct::ParseType(word);
			iss >> word; // name

			if (!word.empty() && word.back() == ';')
			{
				word.pop_back();
			}

			out.SetInp(paraType, 1, word);
			continue;
		}

		// ---------------------------------------------------------------
		// "uniform type name[count];" ???standalone uniform (no layout)
		// ---------------------------------------------------------------
		if (fullLine.find("uniform") != std::string::npos && fullLine.find("layout") == std::string::npos)
		{
			std::istringstream iss(fullLine);
			std::string word;
			iss >> word; // "uniform"

			if (word != "uniform")
			{
				// Not a uniform declaration ---might be a function return
				goto checkFunction;
			}

			iss >> word; // type
			ParaType paraType = ShaderStruct::ParseType(word);
			iss >> word; // name

			if (!word.empty() && word.back() == ';')
			{
				word.pop_back();
			}

			int count = 1;
			size_t bracketPos = word.find('[');
			if (bracketPos != std::string::npos)
			{
				size_t bracketEnd = word.find(']');
				count = std::atoi(word.substr(bracketPos + 1, bracketEnd - bracketPos - 1).c_str());
				word = word.substr(0, bracketPos);
			}

			out.SetUni(paraType, count, word);
			continue;
		}

	checkFunction:

		// ---------------------------------------------------------------
		// "struct Name { ... };" ???struct definition
		// ---------------------------------------------------------------
		if (fullLine.find("struct") != std::string::npos && fullLine.find('=') == std::string::npos)
		{
			std::string structName;

			// Extract struct name: "struct Name {" or "struct Name {"
			size_t nameStart = fullLine.find("struct") + 6;
			while (nameStart < fullLine.size() && std::isspace(static_cast<unsigned char>(fullLine[nameStart])))
			{
				++nameStart;
			}
			size_t nameEnd = nameStart;
			while (nameEnd < fullLine.size() && !std::isspace(static_cast<unsigned char>(fullLine[nameEnd])) && fullLine[nameEnd] != '{' && fullLine[nameEnd] != ';')
			{
				++nameEnd;
			}
			structName = fullLine.substr(nameStart, nameEnd - nameStart);

			ShaderStruct::ADD_TYPE(structName);

		// Determine if '{' is on this line or the next
		bool foundOpenBrace = (fullLine.find('{') != std::string::npos);
		size_t closeBraceOnLine = fullLine.find('}');

		// Single-line struct: "struct Name { type member; };"
		if (foundOpenBrace && closeBraceOnLine != std::string::npos
		    && closeBraceOnLine > fullLine.find('{'))
		{
			size_t braceOpen  = fullLine.find('{');
			size_t braceClose = closeBraceOnLine;
			std::string body = TrimWhitespace(
				fullLine.substr(braceOpen + 1, braceClose - braceOpen - 1));
			if (!body.empty())
			{
				// Parse member: "type name;"
				std::istringstream mstr(body);
				std::string typeName, memberName;
				mstr >> typeName >> memberName;
				if (!memberName.empty() && memberName.back() == ';')
				{
					memberName.pop_back();
				}
				if (!typeName.empty() && !memberName.empty())
				{
			ParaType pType = ShaderStruct::ParseType(typeName);
				argsCache.emplace_back(pType, memberName, typeName);
				}
			}
			out.DefStruct(structName, argsCache);
			argsCache.clear();
			continue;
		}

		argsCache.clear();

		do
		{
			if (!foundOpenBrace)
			{
				if (!std::getline(stream, line))
				{
					NEURUS_ERR("ShaderParser: unexpected EOF while parsing struct definition");
					return false;
				}
				line = StripComments(line, inBlockComment);
				line = TrimWhitespace(line);
				if (line.empty())
				{
					continue;
				}
				if (line.find('{') != std::string::npos)
				{
					foundOpenBrace = true;
					continue;
				}
			}

			if (!std::getline(stream, line))
			{
				NEURUS_ERR("ShaderParser: unexpected EOF while parsing struct members");
				return false;
			}
			line = StripComments(line, inBlockComment);
			line = TrimWhitespace(line);

			// Block ends with "};" or "} varName;" (e.g. push constant / uniform blocks)
			if (line.find('}') != std::string::npos && line.find(';') != std::string::npos)
			{
				break;
			}

			if (line.empty() || line == "{" || line == "}")
			{
				continue;
			}

			// Skip comment-only lines (should already be stripped)
			if (line.find("//") == 0)
			{
				continue;
			}

			// Parse member: "type name;" or "type name; // comment"
			std::istringstream mstr(line);
			std::string typeName;
			mstr >> typeName;
			mstr >> word;

			if (typeName.empty() || word.empty())
			{
				continue;
			}

			if (!word.empty() && word.back() == ';')
			{
				word.pop_back();
			}

			ParaType pType = ShaderStruct::ParseType(typeName);
			argsCache.emplace_back(pType, word, typeName);
			}
			while (true);

			out.DefStruct(structName, argsCache);
			argsCache.clear();
			continue;
		}

	// ---------------------------------------------------------------
	// "const type name = value;" ???constant declaration
	// (Only matches lines that START with "const " ---not "const" in identifiers.)
	// ---------------------------------------------------------------
	if (fullLine.find("const ") == 0)
	{
		std::istringstream iss(fullLine);
		std::string word;
		iss >> word; // "const"
		iss >> word; // type

		ParaType paraType = ShaderStruct::ParseType(word);
		iss >> word; // name

		// Strip trailing ';' if present (may already be gone)
		if (!word.empty() && word.back() == ';')
		{
			word.pop_back();
		}

		// Extract the value: everything after "= " until ";", or emit multi-line skip
		std::string name = word;
		size_t eqPos = fullLine.find('=');
		size_t semiPos = fullLine.find(';');
		std::string value = "0";

		if (eqPos != std::string::npos)
		{
			size_t valStart = eqPos + 1;
			// Multi-line initializer: read until "};" and capture full text
			if (semiPos == std::string::npos
			    && fullLine.find('{', eqPos) != std::string::npos)
			{
				std::string multiValue = TrimWhitespace(
					fullLine.substr(valStart)) + "\n";
				do
				{
					if (!std::getline(stream, line))
					{
						NEURUS_ERR("ShaderParser: unexpected EOF while "
						           "parsing const initializer");
						return false;
					}
					line = StripComments(line, inBlockComment);
					if (line.find('}') != std::string::npos
					    && line.find(';') != std::string::npos)
					{
						// Capture up to and including ';' on closing line
				size_t semi = line.find(';');
					multiValue += line.substr(0, semi); // capture up to ';' (exclusive)
					break;
					}
					multiValue += line + "\n";
				}
				while (true);

				out.SetConst(paraType, name, multiValue);
				continue;
			}
			size_t valEnd = (semiPos != std::string::npos) ? semiPos : fullLine.size();
			value = TrimWhitespace(fullLine.substr(valStart, valEnd - valStart));
		}

		out.SetConst(paraType, name, value);
		continue;
	}

		// ---------------------------------------------------------------
		// "void main() { ... }" ???main entry-point body
		// ---------------------------------------------------------------
		if (fullLine.find("void main") != std::string::npos)
		{
			// -- Check for single-line main body: "void main() { body }" --
			size_t openBrace  = fullLine.find('{');
			size_t closeBrace = fullLine.find('}');

			if (openBrace != std::string::npos && closeBrace != std::string::npos && closeBrace > openBrace)
			{
				// Single-line: extract body between braces directly
				size_t bodyStart = openBrace + 1;
				size_t bodyEnd   = closeBrace;
				std::string body = TrimWhitespace(fullLine.substr(bodyStart, bodyEnd - bodyStart));
				if (!body.empty())
				{
					out.Main = body;
				}
				parsed = true;
				continue;
			}

			// -- Multi-line body: brace-balanced line-by-line walk --
			int  braceDepth     = (openBrace != std::string::npos) ? 1 : 0;
			bool skipFirstBrace = (braceDepth == 0);
			parsed = true;

			do
			{
				if (!std::getline(stream, line))
				{
					// EOF without closing brace ???malformed shader
					NEURUS_ERR("ShaderParser: unexpected EOF while parsing void main() body");
					return false;
				}
				line = StripComments(line, inBlockComment);
				// Do NOT trim ---we need the original indentation for the body

				if (line.find('{') != std::string::npos)
				{
					++braceDepth;
				}
				if (line.find('}') != std::string::npos)
				{
					--braceDepth;
				}

				// Skip the standalone opening brace line
				if (skipFirstBrace && line.find('{') != std::string::npos && line.find('}') == std::string::npos)
				{
					skipFirstBrace = false;
					continue;
				}

				// Accumulate body lines (exclude closing brace)
				if (braceDepth != 0)
				{
					out.Main += line + "\n";
				}
			}
			while (braceDepth != 0);

			continue;
		}

		// ---------------------------------------------------------------
		// Fallback: check if line starts with a known type
		// ???function definition or variable declaration
		// ---------------------------------------------------------------
		{
			std::istringstream iss(fullLine);
			std::string firstWord;
			iss >> firstWord;

			if (ShaderStruct::IsAvailType(firstWord) || firstWord == "void")
			{
				// --- Function definition: "type funcName(args) { body }" ---
				if (fullLine.find('(') != std::string::npos)
				{
					size_t leftParen = fullLine.find('(');
					size_t rightParen = fullLine.find(')');

					ParaType returnType = ShaderStruct::ParseType(firstWord);

					// Extract function name
					std::string funcSig = fullLine.substr(firstWord.size(), leftParen - firstWord.size());
					funcSig = TrimWhitespace(funcSig);

					// Extract arguments list
					std::string argsStr;
					if (leftParen + 1 < rightParen)
					{
						argsStr = fullLine.substr(leftParen + 1, rightParen - leftParen - 1) + ", ";
					}

					// Track brace depth to collect function body
					int braceDepth = (fullLine.find('{') != std::string::npos) ? 1 : 0;
					cache.clear();

				do
				{
					if (!std::getline(stream, line))
					{
						NEURUS_ERR("ShaderParser: unexpected EOF while parsing function body");
						return false;
					}
					line = StripComments(line, inBlockComment);

					if (braceDepth != 0)
					{
						cache += line + "\n";
					}

					if (line.find('{') != std::string::npos)
					{
						++braceDepth;
					}
					if (line.find('}') != std::string::npos)
					{
						--braceDepth;
					}
				}
				while (braceDepth != 0);

					// Strip trailing closing brace and newline from cache
					if (!cache.empty() && cache.back() == '\n')
					{
						cache.pop_back();
					}
					if (!cache.empty() && cache.back() == '}')
					{
						cache.pop_back();
					}
					// Strip possible second trailing newline after removing '}'
					if (!cache.empty() && cache.back() == '\n')
					{
						cache.pop_back();
					}

					out.DefFunc(returnType, funcSig, cache, ShaderStruct::ParseArgs(argsStr));
					cache.clear();
					continue;
				}

				// --- Variable declaration: "type name;" or "type name = value;" ---
				{
					std::string typeName = firstWord;
					std::string varName;
					iss >> varName;

					int count = 1;

					// Check for array: "type name[N];"
					size_t bracketPos = varName.find('[');
					if (bracketPos != std::string::npos)
					{
						size_t bracketEnd = varName.find(']');
						std::string countStr = varName.substr(bracketPos + 1, bracketEnd - bracketPos - 1);
						// The count might be a number or a constant expression
						count = std::atoi(countStr.c_str());
						if (count <= 0)
						{
							count = 1; // Non-constant expression ???treat as size 1
						}
						varName = varName.substr(0, bracketPos);
					}

					// Strip trailing semicolon
					if (!varName.empty() && varName.back() == ';')
					{
						varName.pop_back();
					}

					// Handle comma-separated declarations: "type a, b, c;"
					if (!varName.empty() && varName.back() == ',')
					{
						do
						{
							std::string singleVar = varName;
							singleVar.pop_back(); // remove trailing comma
							out.SetVar(typeName, singleVar, count);
						}
						while (iss >> varName);

						// Remove the last extra entry (SetVar was called once more than needed)
						if (!out.vari_list.empty())
						{
							out.vari_list.pop_back();
						}
					}
					else if (!varName.empty())
					{
						out.SetVar(typeName, varName, count);
					}
				}
			}
			else if (!firstWord.empty() && firstWord != "//" && firstWord[0] != '#')
			{
				// Unrecognised line ---skip silently (may be a comment, blank, or GLSL
				// construct the parser doesn't understand yet).
			}
		}
	} // end main while loop

	return parsed;
}

} // namespace neurus
