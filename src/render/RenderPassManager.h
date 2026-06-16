#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <span>
#include <vector>

namespace neurus {

/**
 * @brief Orchestrates dynamic rendering passes using VK_KHR_dynamic_rendering.
 *
 * Provides BeginPass/EndPass wrappers around vkCmdBeginRendering/vkCmdEndRendering
 * with per-pass-type preset load/store operations and clear values.
 * No VkRenderPass or VkFramebuffer is used — dynamic rendering only.
 *
 * Stateless: all pass state is carried in the command buffer by the driver.
 *
 * Non-copyable, movable.
 */
class RenderPassManager
{
public:
	/**
	 * @brief Identifies a rendering pass with preset attachment configurations.
	 */
	enum class PassType
	{
		G_BUFFER,   ///< 5 color attachments + depth (Position, Normal, Albedo, MetallicRoughness, extra + Depth)
		LIGHTING,   ///< 1 color attachment, no depth
		SHADOW,     ///< Depth-only (0 color attachments)
		COMPOSITE,  ///< 1 color attachment (DONT_CARE load), no depth
		POST_FX     ///< 1 color attachment (DONT_CARE load), no depth
	};

	RenderPassManager() = default;
	~RenderPassManager() = default;

	// --- Non-copyable, movable ---
	RenderPassManager(const RenderPassManager&) = delete;
	RenderPassManager& operator=(const RenderPassManager&) = delete;
	RenderPassManager(RenderPassManager&&) noexcept = default;
	RenderPassManager& operator=(RenderPassManager&&) noexcept = default;

	/**
	 * @brief Begins a dynamic rendering pass.
	 *
	 * Constructs VkRenderingAttachmentInfo for each attachment using
	 * pass-type-preset load/store ops (CLEAR for start-of-frame passes,
	 * DONT_CARE for intermediate passes) and the provided clear values,
	 * then records vkCmdBeginRendering.
	 *
	 * @param cmdBuf           Command buffer to record into.
	 * @param passType         Pass type (determines load/store ops per attachment).
	 * @param colorImageViews  Color attachment image views (count must match pass type preset).
	 * @param pDepthImageView  Optional depth attachment image view.
	 * @param clearValues      Clear values (color values first, then depth/stencil last).
	 * @param renderExtent     Render area extent (width, height).
	 */
	void BeginPass(vk::CommandBuffer cmdBuf,
	               PassType passType,
	               std::span<const vk::ImageView> colorImageViews,
	               const vk::ImageView* pDepthImageView,
	               std::span<const vk::ClearValue> clearValues,
	               vk::Extent2D renderExtent);

	/**
	 * @brief Ends the current dynamic rendering pass.
	 *
	 * Records vkCmdEndRendering into the command buffer.
	 *
	 * @param cmdBuf Command buffer to record into.
	 */
	void EndPass(vk::CommandBuffer cmdBuf);

	// --- Pass Type Queries ---

	/**
	 * @brief Returns the expected number of color attachments for a pass type.
	 * @param passType Pass type to query.
	 * @return Number of color attachments (G_BUFFER=5, LIGHTING/COMPOSITE/POST_FX=1, SHADOW=0).
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
};

} // namespace neurus
