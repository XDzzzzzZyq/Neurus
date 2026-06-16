#include "TextureLib.h"

namespace neurus {

// Static member definition
std::unordered_map<std::string, std::shared_ptr<Texture>> TextureLib::s_cache;

std::shared_ptr<Texture> TextureLib::LoadTexture(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& physicalDevice,
	vk::Queue queue,
	uint32_t queueFamilyIndex,
	const char* path,
	vk::Format format,
	const SamplerConfig& config)
{
	std::string key(path);

	auto it = s_cache.find(key);
	if (it != s_cache.end())
	{
		return it->second;
	}

	auto tex = std::make_shared<Texture>(
		Texture::FromFile(device, physicalDevice, queue, queueFamilyIndex, path, format, config));

	s_cache[key] = tex;
	return tex;
}

void TextureLib::UnloadTexture(const std::string& path)
{
	s_cache.erase(path);
}

void TextureLib::Clear()
{
	s_cache.clear();
}

size_t TextureLib::CacheSize()
{
	return s_cache.size();
}

} // namespace neurus
