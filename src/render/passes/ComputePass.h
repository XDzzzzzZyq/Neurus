/**
 * @file ComputePass.h
 * @brief Abstract compute shader pass base class.
 *
 * Extracts shared infrastructure (sampler, descriptor pool/sets, barrier
 * transitions, dispatch logic) from LightingPass and SSAOPass into a common
 * base class.  Subclasses implement WriteDescriptors() to define their
 * own descriptor bindings and create their compute pipelines.
 *
 * Architecture:
 * - Inherits from Pass (non-copyable, movable, pure virtual Record).
 * - Owns the descriptor set layout, pool, sets, sampler, and pipeline builder.
 * - Subclasses own their compute pipeline, push constants, and any pass-
 *   specific UBOs / SSBOs.
 *
 * @note m_descriptorSetLayout is moved in via the constructor — subclasses
 *       create their own layout via their static CreateDescriptorSetLayout()
 *       factory before calling this constructor.
 */

#pragma once

#include "Pass.h"
#include "../ComputePipelineBuilder.h"
#include "../DescriptorManager.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <string>
#include <vector>

namespace neurus {

class RenderCache;

/**
 * @brief Abstract base class for compute shader passes.
 *
 * Provides:
 * - Nearest-neighbour sampler for G-Buffer reads
 * - Descriptor pool / set lifecycle (pool creation, allocation, debug names)
 * - Compute dispatch helper (bind pipeline → push constants → dispatch)
 *
 * Subclasses must:
 * - Create their own descriptor set layout (static factory)
 * - Implement WriteDescriptors() to write pass-specific bindings
 * - Create their compute pipeline (pass-specific SPIR-V)
 * - Implement Record() from Pass
 */
class ComputePass : public Pass
{
public:
	/**
	 * @brief Constructs the compute pass infrastructure.
	 *
	 * @param device              Logical device (retained reference).
	 * @param physicalDevice      Physical device (for sampler creation).
	 * @param descriptorSetLayout Descriptor set layout created by the subclass (moved in).
	 * @param numSets             Number of descriptor sets (one per in-flight frame).
	 */
	ComputePass(const vk::raii::Device& device,
	            const vk::raii::PhysicalDevice& physicalDevice,
	            DescriptorSetLayout&& descriptorSetLayout,
	            uint32_t numSets);

	~ComputePass() override = default;

	// --- Non-copyable, movable (inherited from Pass) ---
	ComputePass(ComputePass&&) noexcept = default;
	ComputePass& operator=(ComputePass&&) noexcept = default;

	/**
	 * @brief Writes all descriptor bindings into the specified set.
	 *
	 * Called every frame before dispatch.  One set per in-flight frame
	 * prevents updating a set while the GPU is still reading it.
	 *
	 * @param setIndex  Index into m_descriptorSets (0 … numSets-1).
	 */
	virtual void WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache) = 0;

protected:
	// -------------------------------------------------------------------
	// Factory helpers
	// -------------------------------------------------------------------

	/**
	 * @brief Creates a nearest-neighbour sampler for G-Buffer reads.
	 *
	 * Byte-identical to LightingPass::CreateSampler():
	 *   - Nearest filtering (mag, min, mipmap)
	 *   - Clamp-to-edge addressing
	 *   - No anisotropy
	 *   - CompareOp::eAlways
	 *   - FloatTransparentBlack border colour
	 *
	 * @param device         Logical device.
	 * @param physicalDevice Physical device (unused, kept for API symmetry).
	 * @return A nearest-neighbour sampler.
	 */
	static vk::raii::Sampler CreateSampler(const vk::raii::Device& device,
	                                       const vk::raii::PhysicalDevice& physicalDevice);

	/**
	 * @brief Creates the descriptor pool, allocates descriptor sets, and
	 *        assigns debug names (in _DEBUG builds).
	 *
	 * Called from the constructor body after m_descriptorSetLayout is set.
	 *
	 * @param numSets Number of descriptor sets to allocate.
	 */
	void CreateDescriptorSets(uint32_t numSets);

	// -------------------------------------------------------------------
	// Dispatch helper
	// -------------------------------------------------------------------

	/**
	 * @brief Binds the pipeline and descriptor set, pushes constants, and
	 *        dispatches at ceil(extent/16) × ceil(extent/16) thread groups.
	 *
	 * Equivalent to:
	 * @code
	 *   cmdBuf.bindPipeline(eCompute, pipeline);
	 *   cmdBuf.bindDescriptorSets(eCompute, layout, 0, {descriptorSet}, {});
	 *   cmdBuf.pushConstants(layout, eCompute, 0, pushSize, pushData);
	 *   cmdBuf.dispatch(ceil(W/16), ceil(H/16), 1);
	 * @endcode
	 *
	 * @param cmdBuf        Command buffer in recording state.
	 * @param extent        Render area dimensions (determines group count).
	 * @param pipeline      Compute pipeline to bind.
	 * @param layout        Pipeline layout (for descriptor set binding + push constants).
	 * @param descriptorSet Descriptor set to bind at set index 0.
	 * @param pushData      Pointer to push constant data (nullable if pushSize==0).
	 * @param pushSize      Size of push constant data in bytes.
	 */
	void DispatchCompute(vk::CommandBuffer cmdBuf,
	                     vk::Extent2D extent,
	                     const vk::raii::Pipeline& pipeline,
	                     vk::PipelineLayout layout,
	                     vk::DescriptorSet descriptorSet,
	                     const void* pushData,
	                     uint32_t pushSize);

	// -------------------------------------------------------------------
	// Members (accessible to subclasses)
	// -------------------------------------------------------------------

	/// Physical device (non-owning reference, for format / memory queries).
	const vk::raii::PhysicalDevice* m_physicalDevice = nullptr;

	/// Nearest-neighbour sampler for G-Buffer reads.
	vk::raii::Sampler m_sampler = nullptr;

	/// Descriptor set layout (moved in from subclass).
	DescriptorSetLayout m_descriptorSetLayout;

	/// Descriptor pool (one per pass, sized for numSets).
	DescriptorPool m_descriptorPool;

	/// Descriptor sets (one per in-flight frame).
	std::vector<DescriptorSet> m_descriptorSets;

	/// Pipeline builder (must outlive the compute pipeline created by subclass).
	std::unique_ptr<ComputePipelineBuilder> m_pipelineBuilder;
};

// ===================================================================
// Inline implementations
// ===================================================================

inline ComputePass::ComputePass(const vk::raii::Device& device,
                                const vk::raii::PhysicalDevice& physicalDevice,
                                DescriptorSetLayout&& descriptorSetLayout,
                                uint32_t numSets)
	: Pass()
	, m_physicalDevice(&physicalDevice)
	, m_sampler(CreateSampler(device, physicalDevice))
	, m_descriptorSetLayout(std::move(descriptorSetLayout))
	, m_pipelineBuilder(std::make_unique<ComputePipelineBuilder>(device))
{
	m_device = &device;
	CreateDescriptorSets(numSets);
}

inline vk::raii::Sampler ComputePass::CreateSampler(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& /*physicalDevice*/)
{
	vk::SamplerCreateInfo samplerCI(
		{},                                    // flags
		vk::Filter::eNearest,                  // magFilter
		vk::Filter::eNearest,                  // minFilter
		vk::SamplerMipmapMode::eNearest,        // mipmapMode
		vk::SamplerAddressMode::eClampToEdge,   // addressModeU
		vk::SamplerAddressMode::eClampToEdge,   // addressModeV
		vk::SamplerAddressMode::eClampToEdge,   // addressModeW
		0.0f,                                   // mipLodBias
		VK_FALSE,                               // anisotropyEnable
		0.0f,                                   // maxAnisotropy
		VK_FALSE,                               // compareEnable
		vk::CompareOp::eAlways,                 // compareOp
		0.0f,                                   // minLod
		0.0f,                                   // maxLod
		vk::BorderColor::eFloatTransparentBlack, // borderColor
		VK_FALSE                                // unnormalizedCoordinates
	);

	return vk::raii::Sampler(device, samplerCI);
}

inline void ComputePass::CreateDescriptorSets(uint32_t numSets)
{
	m_descriptorPool = DescriptorPool(*m_device,
	                                  numSets,
	                                  DescriptorPool::CalculatePoolSizes({&m_descriptorSetLayout}, numSets));
	m_descriptorSets = m_descriptorPool.Allocate(m_descriptorSetLayout, numSets);

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets; ++i)
	{
		const std::string dsName = "ComputePass_Set" + std::to_string(i);
		m_descriptorSets[i].SetDebugName(dsName.c_str());
	}
#endif
}

inline void ComputePass::DispatchCompute(
	vk::CommandBuffer cmdBuf,
	vk::Extent2D extent,
	const vk::raii::Pipeline& pipeline,
	vk::PipelineLayout layout,
	vk::DescriptorSet descriptorSet,
	const void* pushData,
	uint32_t pushSize)
{
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          layout, 0, {descriptorSet}, {});

	if (pushData && pushSize > 0)
	{
		cmdBuf.pushConstants(layout,
		                     vk::ShaderStageFlagBits::eCompute,
		                     0, pushSize, pushData);
	}

	const uint32_t groupCountX = (extent.width  + 15) / 16;
	const uint32_t groupCountY = (extent.height + 15) / 16;
	cmdBuf.dispatch(groupCountX, groupCountY, 1);
}

} // namespace neurus
