/**
 * @file ShaderStruct.cpp
 * @brief Implementation of ShaderStruct — Reset(), static helpers, and setters.
 */

#include "ShaderStruct.h"

#include <sstream>

namespace neurus {

// ---------------------------------------------------------------------------
// Static type table — maps ParaType indices and custom types to GLSL strings
// ---------------------------------------------------------------------------

std::vector<std::string> ShaderStruct::type_table = {
	"float", "int", "bool", "none", "vec2", "vec3", "vec4", "mat3", "mat4", "sampler2D", "samplerCube"
};

// ---------------------------------------------------------------------------
// ParseType — ParaType ↔ string
// ---------------------------------------------------------------------------

std::string ShaderStruct::ParseType(ParaType type)
{
	// Fast path: built-in types from the enum
	if (static_cast<int>(type) >= 0 && static_cast<size_t>(type) < type_table.size())
	{
		return type_table[static_cast<int>(type)];
	}
	return neurus::ToString(type);
}

ParaType ShaderStruct::ParseType(const std::string& type)
{
	// Search the type table first (handles dynamic custom types)
	for (size_t i = 0; i < type_table.size(); ++i)
	{
		if (type_table[i] == type)
		{
			return static_cast<ParaType>(i);
		}
	}

	// Fall back to Parameters.h for built-in types
	ParaType result = neurus::FromString(type);
	if (result != ParaType::NONE)
	{
		return result;
	}

	// Unknown type — register it as a custom type
	ADD_TYPE(type);
	return ParaType::CUSTOM;
}

// ---------------------------------------------------------------------------
// ParseCount — array suffix
// ---------------------------------------------------------------------------

std::string ShaderStruct::ParseCount(int count)
{
	return count > 1 ? "[" + std::to_string(count) + "]" : "";
}

// ---------------------------------------------------------------------------
// ParseArgs — function arguments ↔ GLSL string
// ---------------------------------------------------------------------------

std::string ShaderStruct::ParseArgs(const Args& args)
{
	std::string result = "(";

	if (!args.empty())
	{
		for (const auto& [type, name] : args)
		{
			result += ParseType(type) + " " + name + ", ";
		}
		// Remove trailing ", "
		result.erase(result.end() - 2, result.end());
	}

	return result + ")";
}

Args ShaderStruct::ParseArgs(const std::string& args)
{
	Args result;
	std::istringstream str(args);
	std::string word;

	while (str >> word)
	{
		ParaType type = ParseType(word);
		str >> word;
		// Remove trailing comma or closing parenthesis
		if (!word.empty() && (word.back() == ',' || word.back() == ')'))
		{
			word.pop_back();
		}
		result.emplace_back(type, word);
	}

	return result;
}

// ---------------------------------------------------------------------------
// IsAvailType — type lookup
// ---------------------------------------------------------------------------

bool ShaderStruct::IsAvailType(const std::string& type)
{
	for (const auto& t : type_table)
	{
		if (t == type)
		{
			return true;
		}
	}
	return neurus::IsKnownType(type);
}

// ---------------------------------------------------------------------------
// ADD_TYPE — register a custom type
// ---------------------------------------------------------------------------

void ShaderStruct::ADD_TYPE(const std::string& name)
{
	// Only add if not already present among custom types (index >= TEXTURE)
	// type_table indices 0-9 are reserved for ParaType enum values
	static constexpr size_t kBuiltinCount = 11; // float through samplerCube

	for (size_t i = kBuiltinCount; i < type_table.size(); ++i)
	{
		if (type_table[i] == name)
		{
			return; // Already registered
		}
	}
	type_table.emplace_back(name);
}

// ---------------------------------------------------------------------------
// Setters — OpenGL-ported (each sets is_struct_changed = true)
// ---------------------------------------------------------------------------

void ShaderStruct::SetAB(int loc, ParaType type, const std::string& name)
{
	is_struct_changed = true;
	AB_list.emplace_back(loc, name, type);
}

void ShaderStruct::SetPass(int loc, ParaType type, const std::string& name)
{
	is_struct_changed = true;
	pass_list.emplace_back(loc, name, type);
}

void ShaderStruct::SetSB(int loc, const std::string& name, const Args& args)
{
	is_struct_changed = true;
	S_StructDef def;
	def.binding = loc;
	def.name = name;
	for (const auto& [argType, argName] : args)
	{
		def.fields.emplace_back(0, argName, argType);
	}
	SB_list.emplace_back(std::move(def));
}

void ShaderStruct::SetUB(std::string type, std::string name, const Args& args)
{
	is_struct_changed = true;
	S_StructDef def;
	def.binding = 0; // Binding will be assigned by the descriptor-layout builder
	def.name = std::move(type);
	def.varName = std::move(name);
	for (const auto& [argType, argName] : args)
	{
		def.fields.emplace_back(0, argName, argType);
	}
	ubuffer_list.emplace_back(std::move(def));
}

void ShaderStruct::SetUni(ParaType type, int count, const std::string& name)
{
	is_struct_changed = true;
	uniform_list.emplace_back(name, type, count);
}

void ShaderStruct::SetInp(ParaType type, int count, const std::string& name)
{
	is_struct_changed = true;
	input_list.emplace_back(name, type, count);
}

void ShaderStruct::SetOut(ParaType type, int count, const std::string& name)
{
	is_struct_changed = true;
	output_list.emplace_back(name, type, count);
}

void ShaderStruct::SetGlob(ParaType type, float defult, const std::string& name)
{
	is_struct_changed = true;
	glob_list.emplace_back(name, type, defult);
}

void ShaderStruct::DefStruct(const std::string& name, const Args& args)
{
	is_struct_changed = true;
	S_StructDef def;
	def.binding = 0; // Bare struct — no binding
	def.name = name;
	for (const auto& [argType, argName] : args)
	{
		def.fields.emplace_back(0, argName, argType);
	}
	struct_def_list.emplace_back(std::move(def));
}

void ShaderStruct::DefFunc(ParaType type, std::string name, const std::string& content, const Args& args)
{
	is_struct_changed = true;
	func_list.emplace_back(type, std::move(name), content, args);
}

void ShaderStruct::SetConst(ParaType type, const std::string& name, const std::string& content)
{
	is_struct_changed = true;
	const_list.emplace_back(type, name, content, Args{});
}

void ShaderStruct::SetVar(const std::string& typeName, const std::string& name, int count)
{
	is_struct_changed = true;
	vari_list.emplace_back(typeName, name, count);
}

// ---------------------------------------------------------------------------
// Vulkan-specific setters
// ---------------------------------------------------------------------------

void ShaderStruct::SetPushConstant(const std::string& name, uint32_t offset, uint32_t size,
                                 const std::string& typeName)
{
	is_struct_changed = true;
	push_constants.push_back({name, offset, size, typeName});
}

void ShaderStruct::SetLocalSize(uint32_t x, uint32_t y, uint32_t z)
{
	is_struct_changed = true;
	local_size_x = x;
	local_size_y = y;
	local_size_z = z;
}

void ShaderStruct::SetVersion(int v)
{
	is_struct_changed = true;
	version = v;
}

void ShaderStruct::AddExtension(const std::string& ext)
{
	is_struct_changed = true;
	// Deduplicate
	for (const auto& existing : extensions)
	{
		if (existing == ext)
		{
			return;
		}
	}
	extensions.push_back(ext);
}

// ---------------------------------------------------------------------------
// Reset — clears ALL containers to default/empty state
// ---------------------------------------------------------------------------

void ShaderStruct::Reset()
{
	AB_list.clear();
	pass_list.clear();
	SB_list.clear();
	ubuffer_list.clear();
	struct_def_list.clear();
	uniform_list.clear();
	input_list.clear();
	output_list.clear();
	const_list.clear();
	glob_list.clear();
	vari_list.clear();
	func_list.clear();

	Main.clear();

	push_constants.clear();
	spec_constants.clear();
	extensions.clear();

	local_size_x = 0;
	local_size_y = 0;
	local_size_z = 0;
	version = 0;

	is_struct_changed = true;
}

// ---------------------------------------------------------------------------
// IsEmpty — true when no data has been set
// ---------------------------------------------------------------------------

bool ShaderStruct::IsEmpty() const
{
	return AB_list.empty()
	    && pass_list.empty()
	    && SB_list.empty()
	    && ubuffer_list.empty()
	    && struct_def_list.empty()
	    && uniform_list.empty()
	    && input_list.empty()
	    && output_list.empty()
	    && const_list.empty()
	    && glob_list.empty()
	    && vari_list.empty()
	    && func_list.empty()
	    && Main.empty()
	    && push_constants.empty()
	    && spec_constants.empty()
	    && extensions.empty()
	    && version == 0
	    && local_size_x == 0;
}

// ---------------------------------------------------------------------------
// GenerateShader — produces valid Vulkan GLSL from the current IR state
// ---------------------------------------------------------------------------

std::string ShaderStruct::GenerateShader()
{
	// Empty shader → minimal stub
	if (IsEmpty())
	{
		return "#version 450 core\nvoid main() {}\n";
	}

	std::ostringstream result;

	// 1. Version
	result << "#version " << (version > 0 ? version : 450) << " core\n\n";

	// 2. Extension directives
	if (!extensions.empty())
	{
		for (const auto& ext : extensions)
		{
			result << "#extension " << ext << " : require\n";
		}
		result << "\n";
	}

	// 3. AB_list — vertex inputs: layout(location = N) in type name;
	if (!AB_list.empty())
	{
		for (const auto& io : AB_list)
		{
			result << "layout(location = " << io.location << ") in "
			       << ParseType(io.type) << " " << io.name << ";\n";
		}
		result << "\n";
	}

	// 4. pass_list — render outputs: layout(location = N) out type name;
	if (!pass_list.empty())
	{
		for (const auto& io : pass_list)
		{
			result << "layout(location = " << io.location << ") out "
			       << ParseType(io.type) << " " << io.name << ";\n";
		}
		result << "\n";
	}

	// 5. struct_def_list — bare struct definitions
	if (!struct_def_list.empty())
	{
		for (const auto& def : struct_def_list)
		{
			result << "struct " << def.name << "\n{\n";
			for (const auto& field : def.fields)
			{
				result << "\t" << ParseType(field.type) << " " << field.name << ";\n";
			}
			result << "};\n\n";
		}
	}

	// 6. SB_list — storage buffers: layout(std430, set=0, binding=B) readonly buffer BName { ... };
	if (!SB_list.empty())
	{
		for (const auto& sb : SB_list)
		{
			result << "layout(std430, set = 0, binding = " << sb.binding
			       << ") readonly buffer " << sb.name << "\n{\n";
			for (const auto& field : sb.fields)
			{
				result << "\t" << ParseType(field.type) << " " << field.name << ";\n";
			}
			result << "};\n";
		}
		result << "\n";
	}

	// 7. ubuffer_list — uniform buffers: layout(std140, set=0, binding=B) uniform UName { ... } var;
	if (!ubuffer_list.empty())
	{
		for (const auto& ub : ubuffer_list)
		{
			result << "layout(std140, set = 0, binding = " << ub.binding
			       << ") uniform " << ub.name << "\n{\n";
			for (const auto& field : ub.fields)
			{
				result << "\t" << ParseType(field.type) << " " << field.name << ";\n";
			}
			// Emit variable / instance name (fall back to block name if empty)
			result << "} " << (ub.varName.empty() ? ub.name : ub.varName) << ";\n";
		}
		result << "\n";
	}

	// 8. push_constants — layout(push_constant) uniform PushConstants { ... } pc;
	if (!push_constants.empty())
	{
		result << "layout(push_constant) uniform PushConstants\n{\n";
		for (const auto& pc : push_constants)
		{
			result << "\t" << pc.typeName << " " << pc.name << ";\n";
		}
		result << "} pc;\n\n";
	}

	// 9. spec_constants — layout(constant_id = B) const type name = defaultVal;
	if (!spec_constants.empty())
	{
		for (const auto& sc : spec_constants)
		{
			result << "layout(constant_id = " << sc.binding << ") const "
			       << ParseType(sc.type) << " " << sc.name << " = "
			       << sc.defaultVal << ";\n";
		}
		result << "\n";
	}

	// 10. uniform_list — uniform type name[N];
	if (!uniform_list.empty())
	{
		for (const auto& u : uniform_list)
		{
			result << "uniform " << ParseType(u.type) << " " << u.name
			       << ParseCount(u.count) << ";\n";
		}
		result << "\n";
	}

	// 11. input_list — in type name[N];
	if (!input_list.empty())
	{
		for (const auto& in : input_list)
		{
			result << "in " << ParseType(in.type) << " " << in.name
			       << ParseCount(in.count) << ";\n";
		}
		result << "\n";
	}

	// 12. output_list — out type name[N];
	if (!output_list.empty())
	{
		for (const auto& out : output_list)
		{
			result << "out " << ParseType(out.type) << " " << out.name
			       << ParseCount(out.count) << ";\n";
		}
		result << "\n";
	}

	// 13. glob_list — type name = type(default);
	if (!glob_list.empty())
	{
		for (const auto& g : glob_list)
		{
			result << ParseType(g.type) << " " << g.name << " = "
			       << ParseType(g.type) << "(" << g.defaultVal << ");\n";
		}
		result << "\n";
	}

	// 14. const_list — const type name = value;
	if (!const_list.empty())
	{
		for (const auto& c : const_list)
		{
			result << "const " << ParseType(c.returnType) << " " << c.name
			       << " = " << c.body << ";\n";
		}
		result << "\n";
	}

	// 15. vari_list — typeName name[N];
	if (!vari_list.empty())
	{
		for (const auto& v : vari_list)
		{
			result << v.typeName << " " << v.name << ParseCount(v.count) << ";\n";
		}
		result << "\n";
	}

	// 16. func_list — returnType name(args) { body }
	if (!func_list.empty())
	{
		for (const auto& f : func_list)
		{
			result << ParseType(f.returnType) << " " << f.name
			       << ParseArgs(f.args) << "\n{\n"
			       << f.body << "\n}\n\n";
		}
	}

	// 17. Compute workgroup size (only for compute shaders)
	if (local_size_x > 0)
	{
		result << "layout(local_size_x = " << local_size_x
		       << ", local_size_y = " << local_size_y
		       << ", local_size_z = " << local_size_z << ") in;\n\n";
	}

	// 18. main() entry point
	// Strip trailing newline from Main (accumulator always adds one after each line)
	std::string mainBody = Main;
	if (!mainBody.empty() && mainBody.back() == '\n')
	{
		mainBody.pop_back();
	}
	if (mainBody.empty())
	{
		result << "void main()\n{\n}\n";
	}
	else
	{
		result << "void main()\n{\n" << mainBody << "\n}\n";
	}

	is_struct_changed = false;
	return result.str();
}

} // namespace neurus
