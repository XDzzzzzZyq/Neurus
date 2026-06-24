/**
 * @file Pass.h
 * @brief Abstract base class for all render passes.
 *
 * Every render pass in the deferred pipeline (GeometryPass, LightingPass,
 * etc.) inherits from this interface.  It enforces a common entry point
 * via Record() and ensures non-copyable RAII semantics for all GPU-
 * owning passes.
 *
 * @note RenderContext is forward-declared here; its definition lives in
 *       RenderContext.h.
 */

#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace neurus {

// --- Forward declarations ---
struct RenderContext;
class RenderCache;

/**
 * @brief Base class for a single render pass in the pipeline.
 *
 * Derived classes implement Record() to write commands into the provided
 * command buffer.  Each pass owns its own GPU resources (pipelines,
 * descriptor sets, buffers) and exposes no Init()/Terminate() methods.
 *
 * @note Non-copyable, movable (copy = delete, move = default).
 */
class Pass
{
public:
	virtual ~Pass() = default;

	// --- Non-copyable, movable ---
	Pass(const Pass&) = delete;
	Pass& operator=(const Pass&) = delete;
	Pass(Pass&&) noexcept = default;
	Pass& operator=(Pass&&) noexcept = default;

	/**
	 * @brief Records the pass's commands into a command buffer.
	 *
	 * Each derived pass receives the per-frame context (immutable) and a
	 * mutable cache reference so it can lazily create or retrieve GPU
	 * resources (pipelines, descriptor sets, buffers) during recording.
	 *
	 * @param cmdBuf   Command buffer in the recording state.
	 * @param cache    Mutable render cache for lazy GPU resource creation.
	 * @param ctx      Per-frame context (attachments, viewport, frame index, etc.).
	 */
	virtual void Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx) = 0;

protected:
	Pass() = default;

	// --- Device reference (non-owning) ---
	const vk::raii::Device* m_device = nullptr;
};

} // namespace neurus
