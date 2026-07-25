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
 * - Caller creates diffuse cubemap (64Â², 1 mip) and specular cubemap
 *   (2048Â², 8 mips) externally
 * - Generates compute pipelines for irradiance and specular convolution
 * - Writes compute results into caller-provided output Images
 * - Cubemap ownership (Images + Samplers) belongs to the caller
 *   (e.g. DeferredRenderer or test fixture)
 */

#pragma once

#include "../DescriptorManager.h"
#include "../shaders/ShaderLibrary.h"
#include "../shaders/ComputeShader.h"
#include "ComputePass.h"

#include <vulkan/vulkan_raii.hpp>

#include <memory>
#include <vector>

namespace neurus {

// --- Forward declarations ---
class Image;
class RenderCache;

/**
 * @brief IBL generation pass - equirect â†?diffuse + specular cubemaps.
 *
 * Non-copyable, movable.
 */
class IBLPass : public ComputePass
{
public:
	/** Diffuse irradiance cubemap face resolution. */
	static constexpr uint32_t kDiffuseFaceRes = 64;
	/** Specular prefiltered cubemap face resolution (mip 0). */
	static constexpr uint32_t kSpecularFaceRes = 2048;
	/** Specular cubemap mip level count (roughness 0..1 â†?mip 0..7). */
	static constexpr uint32_t kSpecularMipLevels = 8;
	/** Default max step count for irradiance convolution. */
	static constexpr int32_t kDefaultIrradianceSteps = 64;
	/** Default max step count for specular prefilter. */
	static constexpr int32_t kDefaultSpecularSteps = 32;

	/**
	 * @brief Constructs the IBL pass - creates samplers, descriptor sets,
	 *        and compute pipelines.
	 *
	 * Shaders are self-loaded via ShaderLibrary from GLSL source files.
	 * Does NOT create cubemap Images - the caller provides those to
	 * Generate().
	 *
	 * @param device          Logical device (retained reference).
	 * @param physicalDevice  Physical device for memory/sampler queries.
	 * @param graphicsQueue   Graphics queue for one-shot command submits.
	 * @param queueFamilyIndex Queue family index for transient cmd pools.
	 */
	IBLPass(const vk::raii::Device& device,
	        const vk::raii::PhysicalDevice& physicalDevice);

	~IBLPass();

	/** @brief No-op â€?IBL generation is one-shot via Generate(), not per-frame. */
	void Record(vk::CommandBuffer cmd, RenderCache& cache, const RenderContext& ctx) override
	{
		(void)cmd;
		(void)cache;
		(void)ctx;
	}

	/** @brief No-op â€?IBLPass manages its own descriptor writes via Generate(). */
	void WriteDescriptors(uint32_t, vk::Extent2D, RenderCache&) override {}

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
	 * - diffuseOut: 64Â², 6-layer Cube, 1 mip, R32G32B32A32_SFLOAT
	 * - specularOut: 2048Â², 6-layer Cube, 8 mips, R32G32B32A32_SFLOAT
	 *   (Images must have eStorage | eSampled usage)
	 *
	 * Records one-shot command buffers for irradiance convolution (1 dispatch)
	 * and specular prefilter (kSpecularMipLevels dispatches, one per mip).
	 *
	 * @param graphicsQueue   Graphics queue for one-shot command submits.
	 * @param queueFamilyIndex Queue family index for transient cmd pools.
	 * @param equirectImage  Equirectangular HDR panorama (2D image).
	 * @param diffuseOut     Pre-created diffuse irradiance cubemap Image.
	 * @param specularOut    Pre-created specular prefiltered cubemap Image.
	 */
	void Generate(vk::Queue graphicsQueue, uint32_t queueFamilyIndex,
	              const Image& equirectImage, Image& diffuseOut, Image& specularOut);

	/** @brief Static factory: creates a linear-clamp equirect sampler. */
	static vk::raii::Sampler CreateEquirectSampler(const vk::raii::Device& device);

private:
	// --- Pipeline / descriptor helpers ---
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);

	void BuildPipeline(const vk::raii::Device& device,
	                   const std::string& debugName) override;

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

	// --- Self-loaded compute shaders (via ShaderLibrary) ---
	std::unique_ptr<ComputeShader> p_irradianceShader;
	std::unique_ptr<ComputeShader> p_specularShader;

	// (Pipelines inherited from Pass â€?p_pipelines[0]=irradiance, p_pipelines[1]=specular)
};

} // namespace neurus
