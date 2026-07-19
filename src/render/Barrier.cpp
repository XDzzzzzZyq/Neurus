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

	case ImageState::Invalid:
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
	image.im_state = after;
}

// ---------------------------------------------------------------------------
// Transition (explicit subresource range — does NOT update im_state)
// ---------------------------------------------------------------------------

void Barrier::Transition(VkCommandBuffer cmd,
                          Image& image,
                          ImageState after,
                          const vk::ImageSubresourceRange& subresourceRange)
{
	const ImageState before = image.im_state;

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

// ---------------------------------------------------------------------------
// Transition (raw vk::Image with explicit before/after — no state tracking)
// ---------------------------------------------------------------------------

void Barrier::Transition(VkCommandBuffer cmd,
                         vk::Image image,
                         ImageState before,
                         ImageState after,
                         const vk::ImageSubresourceRange& subresourceRange)
{
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
		image,
		subresourceRange);

	const vk::DependencyInfo depInfo({}, {}, {}, barrier);
	vk::CommandBuffer(cmd).pipelineBarrier2(depInfo);
}

// ---------------------------------------------------------------------------
// Buffer state → Vulkan mapping
// ---------------------------------------------------------------------------

namespace {

struct VulkanBufferState
{
	vk::PipelineStageFlags2 stage;
	vk::AccessFlags2 access;
};

VulkanBufferState ToVulkanBufferState(BufferState state)
{
	switch (state)
	{
	case BufferState::Undefined:
		return { vk::PipelineStageFlagBits2::eTopOfPipe,
		         vk::AccessFlagBits2::eNone };

	case BufferState::HostWrite:
		return { vk::PipelineStageFlagBits2::eHost,
		         vk::AccessFlagBits2::eHostWrite };

	case BufferState::HostRead:
		return { vk::PipelineStageFlagBits2::eHost,
		         vk::AccessFlagBits2::eHostRead };

	case BufferState::TransferSrc:
		return { vk::PipelineStageFlagBits2::eTransfer,
		         vk::AccessFlagBits2::eTransferRead };

	case BufferState::TransferDst:
		return { vk::PipelineStageFlagBits2::eTransfer,
		         vk::AccessFlagBits2::eTransferWrite };

	case BufferState::ShaderRead:
		return { vk::PipelineStageFlagBits2::eVertexShader |
		         vk::PipelineStageFlagBits2::eFragmentShader |
		         vk::PipelineStageFlagBits2::eComputeShader,
		         vk::AccessFlagBits2::eShaderRead };

	case BufferState::ShaderWrite:
		return { vk::PipelineStageFlagBits2::eComputeShader,
		         vk::AccessFlagBits2::eShaderWrite };

	case BufferState::General:
		return { vk::PipelineStageFlagBits2::eAllCommands,
		         vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite };
	}

	return { vk::PipelineStageFlagBits2::eTopOfPipe,
	         vk::AccessFlagBits2::eNone };
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Buffer transition (state-tracked)
// ---------------------------------------------------------------------------

void Barrier::Transition(VkCommandBuffer cmd,
                         Buffer& buffer,
                         BufferState after)
{
	const BufferState before = buffer.b_state;

	const auto beforeState = ToVulkanBufferState(before);
	const auto afterState  = ToVulkanBufferState(after);

	const vk::BufferMemoryBarrier2 barrier(
		beforeState.stage, beforeState.access,
		afterState.stage, afterState.access,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
		buffer.buffer(),
		0, VK_WHOLE_SIZE);

	const vk::DependencyInfo depInfo({}, {}, barrier, {});
	vk::CommandBuffer(cmd).pipelineBarrier2(depInfo);

	buffer.b_state = after;
}

} // namespace neurus
