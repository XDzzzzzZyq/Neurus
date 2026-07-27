#pragma once

#include <string>

namespace neurus
{

enum class Component : int
{
	Attributes  = 0,  // AB_list
	PassOutputs = 1,  // pass_list
	Inputs      = 2,  // input_list
	Outputs     = 3,  // output_list
	Uniforms    = 4,  // uniform_list
	StructDefs  = 5,  // struct_def_list
	Functions   = 6,  // func_list
};

struct ShaderCreateRequested
{
	int objectId;
};

struct ShaderCompileRequested
{
	int objectId;
	int shaderType;
	int unitType;  // 0 = Code, 1 = Struct
};

struct ShaderCodeEdited
{
	int objectId;
	int shaderType;
	std::string code;
};

struct ShaderSaveRequested
{
	int objectId;
};

struct ShaderModified
{
	int objectId;
	int shaderType;
	int sectionType;    // maps to ShaderStruct lists: 0=AB_list, 1=pass_list, 2=input_list, etc.
	int fieldIndex;     // index within the list
	std::string field;  // "type" or "name"
	std::string value;  // new value
};

} // namespace neurus
