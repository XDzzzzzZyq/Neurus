/**
 * @file GPUProfiler.h
 * @brief GPU timestamp query pool for per-frame, per-pass GPU timing.
 *
 * Owns one vk::raii::QueryPool sized for kMaxFramesInFlight frames; each
 * frame slot holds kMaxPasses + 2 timestamp queries:
 *   slot[0]               frame start (written by the renderer)
 *   slot[1 + i]           end of pass i (i in [0, kMaxPasses))
 *   slot[1 + kMaxPasses]  frame end   (written by the renderer)
 *
 * Pass timings are derived from consecutive boundary timestamps, so only
 * one timestamp per pass is needed (the command buffer executes passes
 * back-to-back). Collect() reads results non-blockingly after the frame's
 * fence signals; VK_NOT_READY (startup / skipped frames) returns false so
 * the caller keeps the previous profile.
 *
 * Opt-in: the pool is only created when the device reports
 * `timestampComputeAndGraphics` and the graphics queue family reports
 * timestamp support.
 */

#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace neurus {

class GPUProfiler
{
public:
	/** @brief Upper bound on instrumented passes per frame (graph slots). */
	static constexpr uint32_t kMaxPasses = 64;

	GPUProfiler() = default;
	~GPUProfiler() = default;

	GPUProfiler(const GPUProfiler&)            = delete;
	GPUProfiler& operator=(const GPUProfiler&) = delete;
	GPUProfiler(GPUProfiler&&) noexcept = default;
	GPUProfiler& operator=(GPUProfiler&&) noexcept = default;

	/**
	 * @brief Creates the timestamp query pool.
	 *
	 * Safe to call once at renderer construction; no-op (Available() == false)
	 * when the device or graphics queue family does not support timestamps.
	 */
	void Initialize(const vk::raii::Device& device,
	                const vk::raii::PhysicalDevice& physicalDevice,
	                uint32_t graphicsQueueFamily,
	                uint32_t maxFramesInFlight);

	/** @brief True when the device supports GPU timestamp queries. */
	bool Available() const { return m_available; }

	/** @brief Timestamp period in nanoseconds (from device limits). */
	double TimestampPeriodNs() const { return m_timestampPeriodNs; }

	/** @brief GPU-resets the current frame slot's query range. */
	void ResetQueries(vk::CommandBuffer cmd, uint32_t frameIndex) const;

	/** @brief Records the frame-start timestamp (before the graph executes). */
	void WriteFrameStart(vk::CommandBuffer cmd, uint32_t frameIndex) const;

	/** @brief Records the end timestamp for pass @p passIndex. */
	void WritePassEnd(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t passIndex) const;

	/** @brief Records the frame-end timestamp (after the final blit). */
	void WriteFrameEnd(vk::CommandBuffer cmd, uint32_t frameIndex) const;

	/**
	 * @brief Reads back the frame slot's timestamps.
	 * @param frameIndex Frame slot whose fence has signaled.
	 * @param passCount  Number of passes recorded in that frame.
	 * @param passGpuMs  Out: per-pass GPU ms (size == passCount).
	 * @param frameGpuMs Out: total GPU ms for the frame.
	 * @return false when the results are not available yet (keep previous).
	 */
	bool Collect(uint32_t frameIndex, uint32_t passCount,
	             std::vector<double>& passGpuMs, double& frameGpuMs) const;

private:
	static uint32_t QueriesPerFrame() { return kMaxPasses + 2; }
	uint32_t QueryOffset(uint32_t frameIndex) const { return frameIndex * QueriesPerFrame(); }

	std::unique_ptr<vk::raii::QueryPool> m_pool;
	bool     m_available = false;
	double   m_timestampPeriodNs = 1.0;
	uint32_t m_maxFramesInFlight = 2;
	mutable std::vector<uint64_t> m_results;
};

} // namespace neurus