#include "ShaderModule.h"

#include <fstream>
#include <stdexcept>

namespace neurus {

ShaderModule::ShaderModule(const vk::raii::Device& device, const std::vector<uint32_t>& spirv)
{
	vk::ShaderModuleCreateInfo createInfo({}, spirv.size() * sizeof(uint32_t), spirv.data());
	m_module = std::make_unique<vk::raii::ShaderModule>(device, createInfo);
}

ShaderModule::~ShaderModule()
{
	// vk::raii handles cleanup automatically
}

ShaderModule ShaderModule::FromFile(const vk::raii::Device& device, const std::string& path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		throw std::runtime_error("Failed to open SPIR-V file: " + path);
	}

	const size_t fileSize = static_cast<size_t>(file.tellg());
	if (fileSize % sizeof(uint32_t) != 0)
	{
		throw std::runtime_error("SPIR-V file size is not aligned to uint32_t: " + path);
	}

	std::vector<uint32_t> spirv(fileSize / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(fileSize));

	if (file.fail())
	{
		throw std::runtime_error("Failed to read SPIR-V file: " + path);
	}

	return ShaderModule(device, spirv);
}

ShaderModule ShaderModule::FromEmbedded(const vk::raii::Device& device, const uint32_t* data, size_t size)
{
	const size_t numWords = size / sizeof(uint32_t);
	std::vector<uint32_t> spirv(data, data + numWords);
	return ShaderModule(device, spirv);
}

} // namespace neurus
