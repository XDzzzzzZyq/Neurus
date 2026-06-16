#include "ShaderLib.h"

namespace neurus {

std::shared_ptr<ShaderModule> ShaderLib::LoadShader(
	const vk::raii::Device& device,
	const std::string& name,
	const uint32_t* data,
	size_t size)
{
	auto it = s_cache.find(name);
	if (it != s_cache.end())
	{
		return it->second;
	}

	auto module = std::make_shared<ShaderModule>(ShaderModule::FromEmbedded(device, data, size));
	s_cache[name] = module;
	return module;
}

void ShaderLib::Clear()
{
	s_cache.clear();
}

} // namespace neurus
