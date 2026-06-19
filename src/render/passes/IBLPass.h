/**
 * @file IBLPass.h
 * @brief Image-Based Lighting generation pass.
 *
 * Converts an equirectangular HDR image into diffuse irradiance and
 * specular prefiltered cubemaps for PBR IBL evaluation.
 *
 * Owns the cubemap Images, samplers, and compute pipelines.
 * One-shot generation (not per-frame) – call Generate() once after
 * loading the HDR environment map.
 *
 * Architecture:
 * - Owns diffuse cubemap (64², 1 mip) and specular cubemap (2048², 8 mips)
 * - Creates compute pipelines for irradiance and specular convolution
 * - Provides ImageViews + Samplers for LightingPass descriptor binding
 * - Provides Save*() methods for GPU readback → HDR file
 */

#pragma once

#include "DescriptorManager.h"

#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace neurus {

// --- Forward declarations ---
class Image;
class ComputePipelineBuilder;

/**
 * @brief IBL generation pass – equirect → diffuse + specular cubemaps.
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
	 * @brief Constructs the IBL pass – creates cubemap Images, samplers,
	 *        descriptor sets, and compute pipelines.
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
	 * Records one-shot command buffers for irradiance convolution (1 dispatch)
	 * and specular prefilter (kSpecularMipLevels dispatches, one per mip).
	 *
	 * @param equirectImage  Equirectangular HDR panorama (2D image).
	 */
	void Generate(const Image& equirectImage);

	// -------------------------------------------------------------------
	// Output accessors (for LightingPass descriptor binding)
	// -------------------------------------------------------------------

	/** @brief Diffuse irradiance cubemap Image. */
	const Image& GetDiffuseCubemap() const { return *m_diffuseCubemap; }

	/** @brief Specular prefiltered cubemap Image. */
	const Image& GetSpecularCubemap() const { return *m_specularCubemap; }

	/** @brief Sampler for diffuse cubemap (linear, clamp). */
	const vk::raii::Sampler& GetDiffuseSampler() const { return m_diffuseSampler; }

	/** @brief Sampler for specular cubemap (linear mipmapped, clamp). */
	const vk::raii::Sampler& GetSpecularSampler() const { return m_specularSampler; }

	// -------------------------------------------------------------------
	// Saving
	// -------------------------------------------------------------------

	/**
	 * @brief Saves the diffuse irradiance cubemap as 6 .hdr face files.
	 * @param pathPrefix  Output path prefix (e.g. "ibl/diffuse")
	 * @return true if all 6 faces were saved successfully.
	 */
	bool SaveDiffuseCubemap(const std::string& pathPrefix);

	/**
	 * @brief Saves the specular cubemap mip 0 as 6 .hdr face files.
	 * @param pathPrefix  Output path prefix (e.g. "ibl/specular")
	 * @return true if all 6 faces were saved successfully.
	 */
	bool SaveSpecularCubemap(const std::string& pathPrefix);

private:
	// --- Cubemap creation ---
	void createCubemaps();

	// --- Pipeline / descriptor helpers ---
	static DescriptorSetLayout CreateDescriptorSetLayout(const vk::raii::Device& device);
	static vk::raii::Sampler CreateEquirectSampler(const vk::raii::Device& device);
	static vk::raii::Sampler CreateCubemapSampler(const vk::raii::Device& device, uint32_t mipLevels);

	vk::raii::Pipeline CreatePipeline(const vk::raii::Device& device,
	                                  const uint32_t* compSpv,
	                                  size_t compSize,
	                                  std::unique_ptr<ComputePipelineBuilder>& outBuilder);

	void WriteDescriptors(const Image& equirectImage, uint32_t specularMip);

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

	// --- Owned cubemaps ---
	std::unique_ptr<Image> m_diffuseCubemap;   // 64², 1 mip, Cube
	std::unique_ptr<Image> m_specularCubemap;  // 2048², 8 mips, Cube
	/** Per-mip image views for specular cubemap (index 0 = mip 0, etc.). */
	std::vector<vk::raii::ImageView> m_specularMipViews;

	// --- Samplers ---
	vk::raii::Sampler m_equirectSampler;   // linear clamp (for HDR source reads)
	vk::raii::Sampler m_diffuseSampler;    // linear clamp (1 mip)
	vk::raii::Sampler m_specularSampler;   // linear mipmapped clamp (8 mips)

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
