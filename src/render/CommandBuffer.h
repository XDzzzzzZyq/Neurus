#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <memory>

namespace neurus {

/**
 * @brief RAII wrapper around a single Vulkan command buffer with convenience
 *        methods for common recording operations.
 *
 * Allocates one command buffer from the given pool using
 * vk::raii::CommandBuffers (count=1) and caches the raw vk::CommandBuffer
 * handle for efficient recording. The command pool must outlive this object
 * (the Renderer layer owns the pool).
 *
 * All recording methods are thin delegates to the corresponding vkCmd*
 * functions. No state tracking beyond a recording flag.
 *
 * Usage:
 *   CommandBuffer cmdBuf(device, pool);
 *   cmdBuf.Begin();
 *   cmdBuf.SetViewport(0, viewport);
 *   cmdBuf.Draw(3, 1, 0, 0);
 *   cmdBuf.End();
 *   cmdBuf.Submit(queue);
 *   device.waitIdle();
 *   cmdBuf.Reset();
 */
class CommandBuffer
{
public:
	/**
	 * @brief Allocates one command buffer from the given pool.
	 *
	 * @param device Logical device (must outlive this object).
	 * @param pool   Command pool (must outlive this object).
	 * @param level  Command buffer level (default: ePrimary).
	 */
	CommandBuffer(const vk::raii::Device& device,
	              const vk::raii::CommandPool& pool,
	              vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary)
		: m_cmdBufs(device, vk::CommandBufferAllocateInfo(*pool, level, 1))
	{
		m_handle = *m_cmdBufs[0];
	}

	~CommandBuffer() = default;

	// Non-copyable — owns a GPU resource allocation
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;

	// Movable
	CommandBuffer(CommandBuffer&&) noexcept = default;
	CommandBuffer& operator=(CommandBuffer&&) noexcept = default;

	// -----------------------------------------------------------------------
	// Lifecycle
	// -----------------------------------------------------------------------

	/** @brief Begins command buffer recording. */
	void Begin(vk::CommandBufferUsageFlags flags = {})
	{
		vk::CommandBufferBeginInfo beginInfo(flags);
		m_handle.begin(beginInfo);
		m_recording = true;
	}

	/** @brief Ends command buffer recording. */
	void End()
	{
		m_handle.end();
		m_recording = false;
	}

	/**
	 * @brief Submits this command buffer to a queue (no semaphores).
	 *
	 * @param queue Graphics / compute queue to submit to.
	 * @param fence Optional fence signaled when the submission completes.
	 *
	 * @note The caller must have called End() before Submit().
	 */
	void Submit(vk::Queue queue, vk::Fence fence = {})
	{
		vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &m_handle, 0, nullptr);
		queue.submit(submitInfo, fence);
	}

	/**
	 * @brief Submits with full vk::SubmitInfo control.
	 *
	 * @param queue      Queue to submit to.
	 * @param submitInfo Submit info (wait semaphores, signal semaphores, etc.).
	 * @param fence      Optional fence signaled on completion.
	 */
	void Submit(vk::Queue queue, const vk::SubmitInfo& submitInfo, vk::Fence fence = {})
	{
		queue.submit(submitInfo, fence);
	}

	/** @brief Resets the command buffer to initial state. */
	void Reset(vk::CommandBufferResetFlags flags = {})
	{
		m_handle.reset(flags);
		m_recording = false;
	}

	// -----------------------------------------------------------------------
	// Copy operations
	// -----------------------------------------------------------------------

	/**
	 * @brief Records a vkCmdCopyBuffer command.
	 * @param src     Source buffer handle.
	 * @param dst     Destination buffer handle.
	 * @param regions One or more buffer copy regions.
	 */
	void CopyBuffer(vk::Buffer src, vk::Buffer dst,
	                const vk::ArrayProxy<const vk::BufferCopy>& regions)
	{
		m_handle.copyBuffer(src, dst, regions);
	}

	/**
	 * @brief Records a vkCmdCopyBufferToImage command.
	 * @param src       Source buffer handle.
	 * @param dst       Destination image handle.
	 * @param dstLayout Current layout of the destination image.
	 * @param regions   One or more buffer-image copy regions.
	 */
	void CopyBufferToImage(vk::Buffer src, vk::Image dst, vk::ImageLayout dstLayout,
	                       const vk::ArrayProxy<const vk::BufferImageCopy>& regions)
	{
		m_handle.copyBufferToImage(src, dst, dstLayout, regions);
	}

	// -----------------------------------------------------------------------
	// Pipeline barriers
	// -----------------------------------------------------------------------

	/**
	 * @brief Records a vkCmdPipelineBarrier command.
	 * @param srcStage        Source pipeline stage flags.
	 * @param dstStage        Destination pipeline stage flags.
	 * @param depFlags        Dependency flags.
	 * @param memoryBarriers  Memory barriers (may be empty).
	 * @param bufferBarriers  Buffer memory barriers (may be empty).
	 * @param imageBarriers   Image memory barriers (may be empty).
	 */
	void PipelineBarrier(vk::PipelineStageFlags srcStage,
	                     vk::PipelineStageFlags dstStage,
	                     vk::DependencyFlags depFlags,
	                     const vk::ArrayProxy<const vk::MemoryBarrier>& memoryBarriers = {},
	                     const vk::ArrayProxy<const vk::BufferMemoryBarrier>& bufferBarriers = {},
	                     const vk::ArrayProxy<const vk::ImageMemoryBarrier>& imageBarriers = {})
	{
		m_handle.pipelineBarrier(
			srcStage, dstStage, depFlags,
			memoryBarriers, bufferBarriers, imageBarriers);
	}

	// -----------------------------------------------------------------------
	// Binding
	// -----------------------------------------------------------------------

	/** @brief Binds a pipeline. */
	void BindPipeline(vk::PipelineBindPoint bindPoint, vk::Pipeline pipeline)
	{
		m_handle.bindPipeline(bindPoint, pipeline);
	}

	/** @brief Binds descriptor sets. */
	void BindDescriptorSets(vk::PipelineBindPoint bindPoint, vk::PipelineLayout layout,
	                        uint32_t firstSet,
	                        const vk::ArrayProxy<const vk::DescriptorSet>& descriptorSets,
	                        const vk::ArrayProxy<const uint32_t>& dynamicOffsets = {})
	{
		m_handle.bindDescriptorSets(bindPoint, layout, firstSet, descriptorSets, dynamicOffsets);
	}

	/** @brief Pushes constants to a pipeline. */
	void PushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags,
	                   uint32_t offset, uint32_t size, const void* data)
	{
		auto byteData = static_cast<const uint8_t*>(data);
		m_handle.pushConstants(layout, stageFlags, offset, vk::ArrayProxy<const uint8_t>(size, byteData));
	}

	// -----------------------------------------------------------------------
	// Draw / Dispatch
	// -----------------------------------------------------------------------

	/** @brief Issues a non-indexed draw command. */
	void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
	          uint32_t firstVertex = 0, uint32_t firstInstance = 0)
	{
		m_handle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	/** @brief Issues an indexed draw command. */
	void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
	                 uint32_t firstIndex = 0, int32_t vertexOffset = 0,
	                 uint32_t firstInstance = 0)
	{
		m_handle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	/** @brief Issues a compute dispatch command. */
	void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
	{
		m_handle.dispatch(groupCountX, groupCountY, groupCountZ);
	}

	// -----------------------------------------------------------------------
	// Dynamic state
	// -----------------------------------------------------------------------

	/** @brief Sets dynamic viewport state. */
	void SetViewport(uint32_t firstViewport,
	                 const vk::ArrayProxy<const vk::Viewport>& viewports)
	{
		m_handle.setViewport(firstViewport, viewports);
	}

	/** @brief Sets dynamic scissor state. */
	void SetScissor(uint32_t firstScissor,
	                const vk::ArrayProxy<const vk::Rect2D>& scissors)
	{
		m_handle.setScissor(firstScissor, scissors);
	}

	// -----------------------------------------------------------------------
	// Accessors
	// -----------------------------------------------------------------------

	/** @brief Returns the raw Vulkan command buffer handle. */
	vk::CommandBuffer handle() const { return m_handle; }

	/** @brief Returns true if the command buffer is currently recording. */
	bool isRecording() const { return m_recording; }

	/**
	 * @brief Provides access to the RAII vector of command buffers.
	 *
	 * The internal CommandBuffers always contains exactly one element.
	 * Use handle() for the raw vk::CommandBuffer.
	 */
	const vk::raii::CommandBuffers& commandBuffers() const { return m_cmdBufs; }

private:
	vk::raii::CommandBuffers m_cmdBufs = nullptr;
	vk::CommandBuffer m_handle = nullptr;
	bool m_recording = false;
};

} // namespace neurus
