/**
 * @file Pass.h
 * @brief Abstract base class for all render passes + pass type queries.
 *
 * Every render pass in the deferred pipeline (GeometryPass, LightingPass,
 * etc.) inherits from this interface.  It enforces a common entry point
 * via Record() and ensures non-copyable RAII semantics for all GPU-
 * owning passes.
 *
 * Also hosts the PassType enum and associated static query helpers that
 * were previously on RenderPassManager.
 *
 * @note RenderContext is forward-declared here; its definition lives in
 *       RenderContext.h.
 */

#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <span>
#include <vector>

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
	/**
	 * @brief Identifies a rendering pass with preset attachment configurations.
	 */
	enum class PassType
	{
		G_BUFFER,   ///< 4 color attachments + depth (Position, Normal, Albedo, MetallicRoughness + Depth)
		LIGHTING,   ///< 1 color attachment, no depth
		SHADOW,     ///< Depth-only (0 color attachments)
		COMPOSITE,  ///< 1 color attachment (DONT_CARE load), no depth
		POST_FX     ///< 1 color attachment (DONT_CARE load), no depth
	};

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

	// --- Pass type queries (moved from RenderPassManager) ---

	/**
	 * @brief Returns the expected number of color attachments for a pass type.
	 * @param passType Pass type to query.
	 * @return Number of color attachments (G_BUFFER=4, LIGHTING/COMPOSITE/POST_FX=1, SHADOW=0).
	 */
	static uint32_t ColorAttachmentCount(PassType passType);

	/**
	 * @brief Returns whether the pass type expects a depth attachment.
	 * @param passType Pass type to query.
	 * @return true for G_BUFFER and SHADOW.
	 */
	static bool HasDepth(PassType passType);

	/**
	 * @brief Returns preset clear values for a given pass type.
	 *
	 * Color clear values come first, depth/stencil clear value last.
	 *
	 * @param passType Pass type to query.
	 * @return Vector of clear values sized to match the pass type attachments.
	 */
	static std::vector<vk::ClearValue> PresetClearValues(PassType passType);

	// --- Attachment load/store helpers ---

	static vk::AttachmentLoadOp  ColorLoadOpFor(PassType passType);
	static vk::AttachmentStoreOp ColorStoreOpFor(PassType passType);
	static vk::AttachmentLoadOp  DepthLoadOpFor(PassType passType);
	static vk::AttachmentStoreOp DepthStoreOpFor(PassType passType);

protected:
	Pass() = default;

	// --- Device reference (non-owning) ---
	const vk::raii::Device* m_device = nullptr;
};

} // namespace neurus
