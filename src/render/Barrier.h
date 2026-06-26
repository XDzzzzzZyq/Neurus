#pragma once

#include "Image.h"

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
	 * @note This does NOT update image.m_state — callers should manage state
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
};

} // namespace neurus
