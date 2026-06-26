#include "Barrier.h"

namespace neurus {

// ---------------------------------------------------------------------------
// ToVulkanImageState
// ---------------------------------------------------------------------------

VulkanImageState Barrier::ToVulkanImageState(ImageState state)
{
	switch (state)
	{
	case ImageState::Undefined:
		return {
			vk::ImageLayout::eUndefined,
			vk::PipelineStageFlagBits2::eTopOfPipe,
			vk::AccessFlagBits2::eNone
		};

	case ImageState::TransferSrc:
		return {
			vk::ImageLayout::eTransferSrcOptimal,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferRead
		};

	case ImageState::TransferDst:
		return {
			vk::ImageLayout::eTransferDstOptimal,
			vk::PipelineStageFlagBits2::eTransfer,
			vk::AccessFlagBits2::eTransferWrite
		};

	case ImageState::ColorAttachment:
		return {
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::AccessFlagBits2::eColorAttachmentWrite
		};

	case ImageState::DepthAttachment:
		return {
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests |
			    vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite
		};

	case ImageState::ColorShaderRead:
		return {
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::PipelineStageFlagBits2::eFragmentShader |
			    vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderRead
		};

	case ImageState::DepthShaderRead:
		return {
			vk::ImageLayout::eDepthStencilReadOnlyOptimal,
			vk::PipelineStageFlagBits2::eFragmentShader |
			    vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderRead
		};

	case ImageState::ShaderWrite:
		return {
			vk::ImageLayout::eGeneral,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderWrite
		};

	case ImageState::Present:
		return {
			vk::ImageLayout::ePresentSrcKHR,
			vk::PipelineStageFlagBits2::eBottomOfPipe,
			vk::AccessFlagBits2::eNone
		};
	}

	// Fallback (should never reach here)
	return {
		vk::ImageLayout::eUndefined,
		vk::PipelineStageFlagBits2::eTopOfPipe,
		vk::AccessFlagBits2::eNone
	};
}

// ---------------------------------------------------------------------------
// Transition (full image)
// ---------------------------------------------------------------------------

void Barrier::Transition(VkCommandBuffer cmd,
                          Image& image,
                          ImageState after)
{
	Transition(cmd, image, after, image.AllSubresources());
	image.m_state = after;
}

// ---------------------------------------------------------------------------
// Transition (explicit subresource range — does NOT update m_state)
// ---------------------------------------------------------------------------

void Barrier::Transition(VkCommandBuffer cmd,
                          Image& image,
                          ImageState after,
                          const vk::ImageSubresourceRange& subresourceRange)
{
	const ImageState before = image.m_state;

	const auto beforeState = ToVulkanImageState(before);
	const auto afterState  = ToVulkanImageState(after);

	const vk::ImageMemoryBarrier2 barrier(
		beforeState.stage,
		beforeState.access,
		afterState.stage,
		afterState.access,
		beforeState.layout,
		afterState.layout,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		*image.ImageHandle(),
		subresourceRange);

	const vk::DependencyInfo depInfo({}, {}, {}, barrier);
	vk::CommandBuffer(cmd).pipelineBarrier2(depInfo);
}

} // namespace neurus
