#pragma once

#include <string>

namespace neurus
{

struct ShaderCreateRequested
{
	int objectId;
};

struct ShaderCompileRequested
{
	int objectId;
};

struct ShaderModified
{
	int objectId;
	int shaderType;
};

struct ShaderSaveRequested
{
	int objectId;
};

struct ShaderFieldEdited
{
	int objectId;
	int shaderType;
	int sectionType;
	int row;
	std::string field;
	std::string value;
};

} // namespace neurus
