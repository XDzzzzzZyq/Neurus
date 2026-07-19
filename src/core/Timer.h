/**
 * @file Timer.h
 * @brief Scoped timer for debug-mode performance instrumentation.
 *
 * prints elapsed wall-clock time via NEURUS_LOG on destruction.
 * Zero runtime cost in Release (NEURUS_LOG compiles to no-op).
 *
 * Usage:
 *   {
 *       NEURUS_TIMER("mesh load");
 *       MeshData::LoadObj(path);   // ... work ...
 *   }                              // prints "[func:line] mesh load: 12.345 ms"
 */

#pragma once

#include "core/Log.h"

#include <chrono>

namespace neurus {

/**
 * @brief Records wall-clock time from construction to destruction.
 *
 * Outputs "label: X.XXX ms" through NEURUS_LOG in debug builds.
 * Measurement precision is high_resolution_clock (sub-microsecond on Windows).
 */
class ScopedTimer
{
public:
	/**
	 * @param label Human-readable operation name (e.g. "shader parse", "mesh GenBuffers").
	 */
	explicit ScopedTimer(std::string label)
		: m_label(std::move(label))
		, m_start(std::chrono::high_resolution_clock::now())
	{}

	~ScopedTimer()
	{
		auto end = std::chrono::high_resolution_clock::now();
		double ms = std::chrono::duration<double, std::milli>(end - m_start).count();
		NEURUS_LOG("[Timer] " << m_label << ": " << ms << " ms");
	}

	// Non-copyable, non-movable
	ScopedTimer(const ScopedTimer&) = delete;
	ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
	std::string m_label;
	std::chrono::high_resolution_clock::time_point m_start;
};

} // namespace neurus

/// Shorthand for a named scoped timer
#define NEURUS_TIMER(label) neurus::ScopedTimer _neurus_timer(label)
