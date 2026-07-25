#include "PipelineCache.h"
#include "core/Log.h"

namespace neurus {

Pipeline* PipelineCache::Get(const std::string& key)
{
	auto it = m_pipelines.find(key);
	return (it != m_pipelines.end()) ? &it->second : nullptr;
}

const Pipeline* PipelineCache::Get(const std::string& key) const
{
	auto it = m_pipelines.find(key);
	return (it != m_pipelines.end()) ? &it->second : nullptr;
}

Pipeline& PipelineCache::GetOrCreate(const std::string& key,
                                     std::function<Pipeline()> factory)
{
	auto it = m_pipelines.find(key);
	if (it != m_pipelines.end())
		return it->second;

	auto [newIt, inserted] = m_pipelines.emplace(key, factory());
	NEURUS_LOG("[PipelineCache] Created pipeline '" << key << "'");
	return newIt->second;
}

void PipelineCache::Store(const std::string& key, Pipeline pipeline)
{
	m_pipelines[key] = std::move(pipeline);
	NEURUS_LOG("[PipelineCache] Stored pipeline '" << key << "'");
}

void PipelineCache::Remove(const std::string& key)
{
	m_pipelines.erase(key);
	NEURUS_LOG("[PipelineCache] Removed pipeline '" << key << "'");
}

void PipelineCache::Clear()
{
	m_pipelines.clear();
	NEURUS_LOG("[PipelineCache] Cleared all pipelines");
}

} // namespace neurus
