/**
 * @file GeometryPass.h
 * @brief G-Buffer geometry pass - renders scene meshes to MRT attachments.
 *
 * GeometryPass records the first phase of a deferred rendering pipeline:
 * it draws all opaque geometry into the G-Buffer colour attachments
 * (Position, Normal, Albedo, MetallicRoughness) and the Depth attachment
 * using VK_KHR_dynamic_rendering.
 *
 * Architecture:
 * - Owns the graphics pipeline, descriptor set layout, descriptor pool,
 *   camera UBO, and camera descriptor set (set 0).
 * - Borrows RenderCache and RenderPassManager (non-owning references).
 * - Receives pre-assembled GeometryRenderItem batches from the caller.
 * - Each GeometryRenderItem bundles GPU buffers + model matrices.
 *
 * @note No PBR lighting - only geometry data is written to the G-Buffer.
 * @note Uses PipelineBuilder for MRT pipeline construction.
 */

#pragma once

#include "../DescriptorManager.h"
#include "../VulkanBuffer.h"
#include "../buffers/BufferLayout.h"
#include "Pass.h"
#include "RenderContext.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vector>

namespace neurus {

// --- Forward declarations ---
class RenderCache;
class RenderPassManager;

/**
 * @brief Camera data uploaded to the GPU each frame.
 *
 * Contains the combined view-projection matrix and the view matrix
 * (needed for view-space normal computation in the vertex shader).
 */
struct CameraUBOData
{
	glm::mat4 viewProj;   ///< projection * view
	glm::mat4 view;       ///< view matrix (for normal transform)
};

/**
 * @brief Per-mesh push-constant block sent to the vertex shader.
 *
 * Packed as two mat4s (128 bytes total) to satisfy Vulkan's
 * 16-byte alignment requirement for push constants.
 */
struct alignas(16) PushConstants
{
	glm::mat4 model;           ///< Local-to-world transform (offset 0)
	glm::mat4 normalMatrix;    ///< 3x3 in upper-left of mat4 (offset 64)
};

/**
 * @brief Single draw batch for the geometry pass.
 *
 * Assembly by the caller (test or Renderer) from a scene Mesh.
 */
struct GeometryRenderItem
{
	vk::Buffer     vertexBuffer;   ///< Vertex buffer handle (binding 0)
	vk::Buffer     indexBuffer;    ///< Index buffer handle
	uint32_t       indexCount;     ///< Number of indices to draw
	vk::IndexType  indexType;      ///< Index type (always UINT32)
	PushConstants  pushConstants;  ///< Model + normal matrix
};

/**
 * @brief G-Buffer geometry pass using dynamic rendering.
 *
 * Creates a graphics pipeline with 4 MRT colour attachments
 * (Position, Normal, Albedo, MetallicRoughness) + depth.
 * Uses PipelineBuilder for pipeline construction and
 * RenderPassManager::PassType::G_BUFFER for pass control.
 *
 * Non-copyable, movable.
 */
class GeometryPass : public Pass
{
public:
	/**
	 * @brief Constructs the geometry pass and creates all GPU resources.
	 *
	 * @param device              Logical device (retained reference).
	 * @param physicalDevice      Physical device (for format queries).
	 * @param queue               Graphics queue (for staging uploads).
	 * @param queueFamilyIndex    Queue family index (for temp command pool).
	 * @param attachmentManager   G-Buffer attachment provider (borrowed).
	 * @param renderPassManager   Dynamic-rendering pass manager (borrowed).
	 * @param vertSpv             Embedded vertex shader SPIR-V data.
	 * @param vertSize            Vertex shader SPIR-V size in bytes.
	 * @param fragSpv             Embedded fragment shader SPIR-V data.
	 * @param fragSize            Fragment shader SPIR-V size in bytes.
	 *
	 * @throws std::runtime_error if shader or pipeline creation fails.
	 */
	GeometryPass(const vk::raii::Device& device,
	             const vk::raii::PhysicalDevice& physicalDevice,
	             vk::Queue queue,
	             uint32_t queueFamilyIndex,
	             RenderPassManager& renderPassManager,
	             const uint32_t* vertSpv,
	             size_t vertSize,
	             const uint32_t* fragSpv,
	             size_t fragSize);

	/**
	 * @brief Records the G-Buffer draw commands into a command buffer.
	 *
	 *   1. Uploads camera data to the UBO (host-visible memcpy).
	 *   2. Begins the G_BUFFER dynamic rendering pass.
	 *   3. Sets viewport and scissor.
	 *   4. For each GeometryRenderItem: pushes model/normal matrices,
	 *      binds vertex/index buffers, binds camera descriptor set,
	 *      and draws indexed.
	 *   5. Ends the dynamic rendering pass.
	 *
	 * @param cmdBuf          Command buffer in recording state.
	 * @param cameraData      Camera matrices for the current frame.
	 * @param renderItems     Vector of draw batches to render.
	 * @param renderExtent    Render area dimensions.
	 */
	void Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx) override;

	/**
	 * @brief Returns the camera descriptor set layout (set 0).
	 */
	const DescriptorSetLayout& GetCameraLayout() const { return m_cameraLayout; }

private:
	/**
	 * @brief Creates the camera descriptor set layout (set 0, binding 0: CameraUBO).
	 */
	static DescriptorSetLayout CreateCameraLayout(const vk::raii::Device& device);

	/**
	 * @brief Creates the graphics pipeline using PipelineBuilder.
	 * Sets m_pipelineLayout as a side effect.
	 */
	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device,
	                                  const uint32_t* vertSpv,
	                                  size_t vertSize,
	                                  const uint32_t* fragSpv,
	                                  size_t fragSize);

	// --- References (non-owning) ---
	const vk::raii::PhysicalDevice* m_physicalDevice;
	RenderPassManager* m_renderPassManager;

	// --- Descriptor resources ---
	DescriptorSetLayout m_cameraLayout;             ///< Set 0 layout definition

	// --- Camera UBO (host-visible for per-frame update) ---
	VulkanBuffer m_cameraUBO;

	// --- Descriptor pool + set for camera UBO ---
	DescriptorPool m_descriptorPool;
	DescriptorSet m_cameraDescriptorSet;

	// --- Pipeline ---
	vk::raii::PipelineLayout m_pipelineLayout;
	vk::raii::Pipeline m_pipeline;

	// --- Vertex input layout ---
	BufferLayout m_vertexLayout;
};

} // namespace neurus
