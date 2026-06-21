/**
 * @file IBLPass.h
 * @brief Image-Based Lighting generation pass - pure compute service.
 *
 * Converts an equirectangular HDR image into diffuse irradiance and
 * specular prefiltered cubemaps for PBR IBL evaluation.
 *
 * Does NOT own cubemap resources - caller provides output Image
 * references.  Owns only compute pipelines and descriptor management.
 * One-shot generation (not per-frame) - call Generate() once after
 * loading the HDR environment map.
 *
 * Architecture:
 * - Caller creates diffuse cubemap (64², 1 mip) and specular cubemap
 *   (2048², 8 mips) externally
 * - Generates compute pipelines for irradiance and specular convolution
 * - Writes compute results into caller-provided output Images
 * - Cubemap ownership (Images + Samplers) belongs to the caller
 *   (e.g. DeferredRenderer or test fixture)
 */

#pragma once

#include "DescriptorManager.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <vector>

namespace neurus {

// --- Forward declarations ---
class Image;
class ComputePipelineBuilder;

/**
 * @brief IBL generation pass - equirect → diffuse + specular cubemaps.
 *
 * Non-copyable, movable.
 */
class IBLPass
{
public:
	/** Diffuse irradiance cubemap face resolution. */
	static constexpr uint32_t kDiffuseFaceRes = 64;
	/** Specular prefiltered cubemap face resolution (mip 0). */
	static constexpr uint32_t kSpecularFaceRes = 2048;
	/** Specular cubemap mip level count (roughness 0..1 → mip 0..7). */
	static constexpr uint32_t kSpecularMipLevels = 8;
	/** Default max step count for irradiance convolution. */
	static constexpr int32_t kDefaultIrradianceSteps = 64;
	/** Default max step count for specular prefilter. */
	static constexpr int32_t kDefaultSpecularSteps = 32;

	/**
	 * @brief Constructs the IBL pass - creates samplers, descriptor sets,
	 *        and compute pipelines.
	 *
	 * Does NOT create cubemap Images - the caller provides those to
	 * Generate().
	 *
	 * @param device          Logical device (retained reference).
	 * @param physicalDevice  Physical device for memory/sampler queries.
	 * @param graphicsQueue   Graphics queue for one-shot command submits.
	 * @param queueFamilyIndex Queue family index for transient cmd pools.
	 * @param irradianceSpv   Embedded SPIR-V for irrandiance_conv.comp.
	 * @param irradianceSize  SPIR-V byte size.
	 * @param specularSpv     Embedded SPIR-V for importance_samp.comp.
	 * @param specularSize    SPIR-V byte size.
	 */
	IBLPass(const vk::raii::Device& device,
	        const vk::raii::PhysicalDevice& physicalDevice,
	        vk::Queue graphicsQueue,
	        uint32_t queueFamilyIndex,
	        const uint32_t* irradianceSpv,
	        size_t irradianceSize,
	        const uint32_t* specularSpv,
	        size_t specularSize);

	~IBLPass();

	// --- Non-copyable, movable ---
	IBLPass(const IBLPass&) = delete;
	IBLPass& operator=(const IBLPass&) = delete;
	IBLPass(IBLPass&&) noexcept = default;
	IBLPass& operator=(IBLPass&&) noexcept = default;

	// -------------------------------------------------------------------
	// Generation
	// -------------------------------------------------------------------

	/**
	 * @brief Generates diffuse + specular cubemaps from an equirect Image.
	 *
	 * The equirect Image must be 2D, R32G32B32A32_SFLOAT, and in
	 * SHADER_READ_ONLY_OPTIMAL layout (or will be transitioned).
	 *
	 * The caller MUST pre-create the output Images:
	 * - diffuseOut: 64², 6-layer Cube, 1 mip, R32G32B32A32_SFLOAT
	 * - specularOut: 2048², 6-layer Cube, 8 mips, R32G32B32A32_SFLOAT
	 *   (Images must have eStorage | eSampled usage)
	 *
	 * Records one-shot command buffers for irradiance convolution (1 dispatch)
	 * and specular prefilter (kSpecularMipLevels dispatches, one per mip).
	 *
	 * @param equirectImage  Equirectangular HDR panorama (2D image).
	 * @param diffuseOut     Pre-created diffuse irradiance cubemap Image.
	 * @param specularOut    Pre-created specular prefiltered cubemap Image.
	 */
	void Generate(const Image& equirectImage, Image& diffuseOut, Image& specularOut);

	/** @brief Static factory: creates a linear-clamp equirect sampler. */
	static vk::raii::Sampler CreateEquirectSampler(const vk::raii::Device& device);

private:
	// --- Pipeline / descriptor helpers ---
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);

	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device,
	                                  const uint32_t* compSpv,
	                                  size_t compSize,
	                                  std::unique_ptr<ComputePipelineBuilder>& outBuilder);

	void WriteDescriptors(const Image& equirectImage,
	                      const vk::raii::Sampler& equirectSampler,
	                      const vk::raii::ImageView& outputView);

	// --- Dispatch helpers ---
	void dispatchCompute(vk::CommandBuffer cmdBuf,
	                     const vk::raii::Pipeline& pipeline,
	                     vk::PipelineLayout pipelineLayout,
	                     uint32_t groupCountX,
	                     uint32_t groupCountY,
	                     uint32_t groupCountZ,
	                     int32_t mipLevel,
	                     int32_t maxStep,
	                     float roughnessSq);

	// --- References (non-owning) ---
	const vk::raii::Device* m_device;
	const vk::raii::PhysicalDevice* m_physicalDevice;
	vk::Queue m_graphicsQueue;
	uint32_t m_queueFamilyIndex;

	// --- Descriptor resources ---
	DescriptorSetLayout m_descriptorSetLayout;
	DescriptorPool m_descriptorPool;
	std::vector<DescriptorSet> m_descriptorSets;  // one set (index 0 used for all dispatches)

	// --- Pipelines ---
	std::unique_ptr<ComputePipelineBuilder> m_irradiancePipelineBuilder;
	vk::raii::Pipeline m_irradiancePipeline;
	std::unique_ptr<ComputePipelineBuilder> m_specularPipelineBuilder;
	vk::raii::Pipeline m_specularPipeline;
};

} // namespace neurus
