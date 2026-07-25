#include "PipelineCache.h"
#include "core/Log.h"

namespace neurus {

Pipeline* PipelineCache::Get(int uid)
{
	auto it = p_pipelines.find(uid);
	return (it != p_pipelines.end()) ? &it->second : nullptr;
}

const Pipeline* PipelineCache::Get(int uid) const
{
	auto it = p_pipelines.find(uid);
	return (it != p_pipelines.end()) ? &it->second : nullptr;
}

Pipeline& PipelineCache::GetOrCreate(int uid, std::function<Pipeline()> factory)
{
	auto it = p_pipelines.find(uid);
	if (it != p_pipelines.end())
		return it->second;

	auto [newIt, inserted] = p_pipelines.emplace(uid, factory());
	NEURUS_LOG("[PipelineCache] Created pipeline for uid=" << uid);
	return newIt->second;
}

void PipelineCache::Store(int uid, Pipeline pipeline)
{
	p_pipelines[uid] = std::move(pipeline);
	NEURUS_LOG("[PipelineCache] Stored pipeline for uid=" << uid);
}

void PipelineCache::Remove(int uid)
{
	p_pipelines.erase(uid);
	NEURUS_LOG("[PipelineCache] Removed pipeline for uid=" << uid);
}

void PipelineCache::Clear()
{
	p_pipelines.clear();
	NEURUS_LOG("[PipelineCache] Cleared all pipelines");
}

} // namespace neurus
