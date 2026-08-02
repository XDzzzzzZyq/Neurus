/**
 * @file GPUProfiler.cpp
 * @brief Implementation of the GPU timestamp query pool.
 */

#include "GPUProfiler.h"

#include "core/Log.h"

#include <algorithm>

namespace neurus {

void GPUProfiler::Initialize(const vk::raii::Device& device,
                             const vk::raii::PhysicalDevice& physicalDevice,
                             uint32_t graphicsQueueFamily,
                             uint32_t maxFramesInFlight)
{
	m_maxFramesInFlight = maxFramesInFlight;
	m_pool.reset();
	m_available = false;

	const vk::PhysicalDeviceProperties props = physicalDevice.getProperties();
	const std::vector<vk::QueueFamilyProperties> queueFamilies =
		physicalDevice.getQueueFamilyProperties();
	const bool queueTimestamps = graphicsQueueFamily < queueFamilies.size() &&
	                             queueFamilies[graphicsQueueFamily].timestampValidBits > 0;

	if (!props.limits.timestampComputeAndGraphics || !queueTimestamps)
	{
		NEURUS_LOG("[GPUProfiler] Timestamp queries unsupported - GPU timing disabled "
		           << "(timestampComputeAndGraphics=" << props.limits.timestampComputeAndGraphics
		           << ", queueTimestampBits="
		           << (graphicsQueueFamily < queueFamilies.size()
		                   ? queueFamilies[graphicsQueueFamily].timestampValidBits : 0) << ")");
		return;
	}

	m_timestampPeriodNs = static_cast<double>(props.limits.timestampPeriod);
	vk::QueryPoolCreateInfo poolCI(vk::QueryPoolCreateFlags{},
	                               vk::QueryType::eTimestamp,
	                               maxFramesInFlight * QueriesPerFrame());
	m_pool = std::make_unique<vk::raii::QueryPool>(device, poolCI);
	m_available = true;

	NEURUS_LOG("[GPUProfiler] Timestamp query pool created ("
	           << maxFramesInFlight << " frames x " << QueriesPerFrame()
	           << " queries, period=" << m_timestampPeriodNs << "ns)");
}

void GPUProfiler::ResetQueries(vk::CommandBuffer cmd, uint32_t frameIndex) const
{
	if (!m_available)
		return;
	cmd.resetQueryPool(**m_pool, QueryOffset(frameIndex), QueriesPerFrame());
}

void GPUProfiler::WriteFrameStart(vk::CommandBuffer cmd, uint32_t frameIndex) const
{
	if (!m_available)
		return;
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,
	                   **m_pool, QueryOffset(frameIndex));
}

void GPUProfiler::WritePassEnd(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t passIndex) const
{
	if (!m_available || passIndex >= kMaxPasses)
		return;
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands,
	                   **m_pool, QueryOffset(frameIndex) + 1 + passIndex);
}

void GPUProfiler::WriteFrameEnd(vk::CommandBuffer cmd, uint32_t frameIndex) const
{
	if (!m_available)
		return;
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands,
	                   **m_pool, QueryOffset(frameIndex) + 1 + kMaxPasses);
}

bool GPUProfiler::Collect(uint32_t frameIndex, uint32_t passCount,
                          std::vector<double>& passGpuMs, double& frameGpuMs) const
{
	if (!m_available || passCount > kMaxPasses)
		return false;

	const uint32_t queryCount = passCount + 2;
	const uint32_t offset = QueryOffset(frameIndex);

	try
	{
		const auto resultValue = m_pool->getResults<uint64_t>(
			offset, queryCount, queryCount * sizeof(uint64_t),
			sizeof(uint64_t), vk::QueryResultFlagBits::e64);
		if (resultValue.result != vk::Result::eSuccess)
			return false; // eNotReady at startup or after skipped frames.
		m_results = resultValue.value;
	}
	catch (const vk::SystemError&)
	{
		return false; // Device lost / out of memory - keep previous profile.
	}

	const double ticksToMs = m_timestampPeriodNs / 1'000'000.0;
	passGpuMs.resize(passCount);

	double previous = static_cast<double>(m_results[0]);
	for (uint32_t i = 0; i < passCount; ++i)
	{
		const double end = static_cast<double>(m_results[1 + i]);
		passGpuMs[i] = std::max(0.0, (end - previous) * ticksToMs);
		previous = end;
	}
	frameGpuMs = std::max(0.0, (static_cast<double>(m_results[1 + passCount]) -
	                            static_cast<double>(m_results[0])) * ticksToMs);
	return true;
}

} // namespace neurus