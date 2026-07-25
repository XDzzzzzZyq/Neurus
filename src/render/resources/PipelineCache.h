#pragma once

#include "Pipeline.h"

#include <functional>
#include <string>
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

	Pipeline* Get(const std::string& key);
	const Pipeline* Get(const std::string& key) const;

	Pipeline& GetOrCreate(const std::string& key,
	                      std::function<Pipeline()> factory);

	void Store(const std::string& key, Pipeline pipeline);
	void Remove(const std::string& key);
	void Clear();

private:
	std::unordered_map<std::string, Pipeline> m_pipelines;
};

} // namespace neurus
