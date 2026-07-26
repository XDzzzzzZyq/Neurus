#pragma once

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
};

struct ShaderSaveRequested
{
	int objectId;
};

} // namespace neurus
