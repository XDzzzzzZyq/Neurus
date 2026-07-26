#pragma once

#include "../Pipeline.h"

#include <functional>
#include <unordered_map>

namespace neurus {

class PipelineCache
{
public:
	PipelineCache() = default;
	~PipelineCache() = default;

	PipelineCache(const PipelineCache&) = delete;
	PipelineCache& operator=(const PipelineCache&) = delete;
	PipelineCache(PipelineCache&&) noexcept = default;
	PipelineCache& operator=(PipelineCache&&) noexcept = default;

	Pipeline* Get(int uid);
	const Pipeline* Get(int uid) const;

	Pipeline& GetOrCreate(int uid,
	                      std::function<Pipeline()> factory);

	void Store(int uid, Pipeline pipeline);
	void Remove(int uid);
	void Clear();

private:
	std::unordered_map<int, Pipeline> p_pipelines;
};

} // namespace neurus
