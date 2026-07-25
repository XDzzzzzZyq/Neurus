#pragma once

#include "Shader.h"
#include "ShaderStruct.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace neurus {

class Shader;
class RenderShader;
class ComputeShader;
struct ShaderUnit;

class ShaderLibrary
{
public:
	ShaderLibrary() = delete;
	~ShaderLibrary() = delete;
	ShaderLibrary(const ShaderLibrary&) = delete;
	ShaderLibrary& operator=(const ShaderLibrary&) = delete;

	// --- Parse (CPU side, no caching) ---
	static std::unique_ptr<RenderShader> ParseRenderShader(
		const std::string& name,
		const std::string& vertPath,
		const std::string& fragPath);

	static std::unique_ptr<ComputeShader> ParseComputeShader(
		const std::string& name,
		const std::string& compPath);

	// --- Compile (GLSL -> SPIR-V) ---
	static std::vector<uint32_t> Compile(
		const ShaderUnit& stage,
		ShaderType type,
		const std::string& debugName);

	static std::unordered_map<ShaderType, std::vector<uint32_t>> CompileAll(
		const Shader& shader);

	// --- Build-in constants ---
	static const S_Const* GetBuildInConstant(const std::string& name);
};

} // namespace neurus
