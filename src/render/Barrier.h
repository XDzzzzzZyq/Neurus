#pragma once

#include "Image.h"
#include "buffers/Buffer.h"

#include <vulkan/vulkan_raii.hpp>

namespace neurus {

/**
 * @brief Maps an ImageState to its corresponding Vulkan layout, stage, and access flags.
 */
struct VulkanImageState
{
	vk::ImageLayout layout;
	vk::PipelineStageFlags2 stage;
	vk::AccessFlags2 access;
};

/**
 * @brief Centralized barrier management for image layout transitions.
 *
 * All image layout transitions should go through Barrier::Transition().
 * Never use raw vk::ImageMemoryBarrier or vk::ImageMemoryBarrier2 directly
 * for Image objects — use Barrier instead.
 *
 * Usage:
 *   Barrier::Transition(cmdBuf, myImage, ImageState::ColorShaderRead);
 */
class Barrier
{
public:
	/**
	 * @brief Converts a logical ImageState to its Vulkan counterparts.
	 *
	 * @param state  The logical image state.
	 * @return VulkanImageState with layout, stage, and access.
	 */
	static VulkanImageState ToVulkanImageState(ImageState state);

	/**
	 * @brief Records a pipeline barrier transitioning an Image to a new state.
	 *
	 * Uses the image's current m_state as the source and @p after as the
	 * destination.  Generates the appropriate vk::ImageMemoryBarrier2 using
	 * ToVulkanImageState() and records it via vkCmdPipelineBarrier2.
	 *
	 * After the barrier, the image's m_state is updated to @p after.
	 *
	 * @param cmd    Command buffer handle (raw VkCommandBuffer).
	 * @param image  Image to transition (m_state is read and updated).
	 * @param after  Target logical state.
	 */
	static void Transition(VkCommandBuffer cmd,
	                       Image& image,
	                       ImageState after);

	/**
	 * @brief Convenience overload for vk::raii::CommandBuffer.
	 */
	static void Transition(const vk::raii::CommandBuffer& cmd,
	                       Image& image,
	                       ImageState after)
	{
		Transition(*cmd, image, after);
	}

	/**
	 * @brief Records a pipeline barrier transitioning an Image, with explicit subresource range.
	 *
	 * Same as Transition(cmd, image, after) but uses @p subresourceRange instead
	 * of the image's default m_subresourceRange.  Useful for per-mip or per-face
	 * transitions (e.g. during mipmap generation).
	 *
	 * @note This does NOT update image.im_state — callers should manage state
	 *       manually for partial transitions or use the simpler overload.
	 *
	 * @param cmd               Command buffer handle (raw VkCommandBuffer).
	 * @param image             Image to transition.
	 * @param after             Target logical state.
	 * @param subresourceRange  Subresource range for the barrier.
	 */
	static void Transition(VkCommandBuffer cmd,
	                       Image& image,
	                       ImageState after,
	                       const vk::ImageSubresourceRange& subresourceRange);

	/**
	 * @brief Convenience overload for vk::raii::CommandBuffer.
	 */
	static void Transition(const vk::raii::CommandBuffer& cmd,
	                       Image& image,
	                       ImageState after,
	                       const vk::ImageSubresourceRange& subresourceRange)
	{
		Transition(*cmd, image, after, subresourceRange);
	}

	/**
	 * @brief Records a pipeline barrier for a raw vk::Image with explicit before/after states.
	 *
	 * For images not wrapped in a neurus::Image (e.g. swapchain images), provides
	 * the same centralized state→layout mapping without tracking state.
	 *
	 * @param cmd               Command buffer handle (raw VkCommandBuffer).
	 * @param image             Raw Vulkan image handle.
	 * @param before            Source logical state.
	 * @param after             Target logical state.
	 * @param subresourceRange  Subresource range for the barrier.
	 */
	static void Transition(VkCommandBuffer cmd,
	                       vk::Image image,
	                       ImageState before,
	                       ImageState after,
	                       const vk::ImageSubresourceRange& subresourceRange);

	/**
	 * @brief Convenience overload for vk::raii::CommandBuffer.
	 */
	static void Transition(const vk::raii::CommandBuffer& cmd,
	                       vk::Image image,
	                       ImageState before,
	                       ImageState after,
	                       const vk::ImageSubresourceRange& subresourceRange)
	{
		Transition(*cmd, image, before, after, subresourceRange);
	}

	/**
	 * @brief Records a release barrier after a transfer-queue image upload.
	 *
	 * Makes transfer writes available to subsequent graphics usage WITHOUT a
	 * graphics-stage dstStageMask, which is invalid on transfer-only command
	 * pools (VUID-vkCmdPipelineBarrier2-dstStageMask-09676). The image layout
	 * still becomes ShaderReadOnlyOptimal so the graphics side can sample it;
	 * the consuming pass re-transitions the image on first use.
	 *
	 * @param cmd               Command buffer (raw VkCommandBuffer).
	 * @param image             Raw Vulkan image handle.
	 * @param before            Source logical state (TransferDst or TransferSrc).
	 * @param subresourceRange  Subresource range for the barrier.
	 */
	static void ReleaseToRead(VkCommandBuffer cmd,
	                          vk::Image image,
	                          ImageState before,
	                          const vk::ImageSubresourceRange& subresourceRange);

	/**
	 * @brief Convenience overload for vk::raii::CommandBuffer.
	 */
	static void ReleaseToRead(const vk::raii::CommandBuffer& cmd,
	                          vk::Image image,
	                          ImageState before,
	                          const vk::ImageSubresourceRange& subresourceRange)
	{
		ReleaseToRead(*cmd, image, before, subresourceRange);
	}

	// --- Buffer barriers (state-tracked) ---

	/**
	 * @brief Records a buffer memory barrier, tracking state.
	 *
	 * Reads buffer.State() as the source, emits a vk::BufferMemoryBarrier2
	 * mapping @p after through ToVulkanBufferState(), and updates
	 * buffer.b_state to @p after.
	 *
	 * Follows the same pattern as Transition(Image&, ...).
	 *
	 * @param cmd    Command buffer (raw VkCommandBuffer).
	 * @param buffer Buffer to transition.
	 * @param after  Target logical buffer state.
	 */
	static void Transition(VkCommandBuffer cmd,
	                       Buffer& buffer,
	                       BufferState after);

	static void Transition(const vk::raii::CommandBuffer& cmd,
	                       Buffer& buffer,
	                       BufferState after)
	{
		Transition(*cmd, buffer, after);
	}
};

} // namespace neurus
