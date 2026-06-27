/**
 * @file SSAOPass.cpp
 * @brief Screen-Space Ambient Occlusion compute pass implementation.
 */

#include "RenderCache.h"
#include "passes/SSAOPass.h"

#include "ComputePipelineBuilder.h"
#include "Image.h"
#include "render/Barrier.h"
#include "RenderContext.h"
#include "shaders/ShaderModule.h"

#include "Log.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace neurus {

// ---------------------------------------------------------------------------
// Random number helpers (inline, local to this TU)
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Simple deterministic random number generator (xorshift32).
 *
 * Used to produce reproducible kernel samples and noise vectors
 * so that reference-image regression tests are deterministic.
 */
class DeterministicRNG
{
public:
	explicit DeterministicRNG(uint32_t seed = 0xDEADBEEF) : m_state(seed) {}

	float rand01()
	{
		m_state ^= m_state << 13;
		m_state ^= m_state >> 17;
		m_state ^= m_state << 5;
		return static_cast<float>(m_state) / static_cast<float>(UINT32_MAX);
	}

	float rand11()
	{
		return rand01() * 2.0f - 1.0f;
	}

	glm::vec3 rand3n()
	{
		return glm::normalize(glm::vec3(rand11(), rand11(), rand11()));
	}

	/**
	 * @brief Random unit hemisphere direction biased toward the normal (z > 0).
	 */
	glm::vec3 rand3nh()
	{
		glm::vec3 v(rand11(), rand11(), rand11());
		v.z = v.z * 0.5f + 0.5f;  // bias z toward [0, 1]
		return glm::normalize(v);
	}

private:
	uint32_t m_state;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SSAOPass::SSAOPass(const vk::raii::Device& device,
                   const vk::raii::PhysicalDevice& physicalDevice,
                   uint32_t numSets,
                   vk::Queue graphicsQueue,
                   uint32_t queueFamilyIndex,
                   const uint32_t* compSpv,
                   size_t compSize)
	: ComputePass(device, physicalDevice,
	              SSAOPass::CreateDescriptorSetLayout(device), numSets)
	, m_pipeline(CreatePipeline(device, compSpv, compSize))
{
	NEURUS_LOG("[SSAOPass] compSize=" << compSize << " numSets=" << numSets
	           << " kernelLength=" << kDefaultKernelLength
	           << " qfi=" << queueFamilyIndex);

	// --- Generate and upload kernel + initial camera data ---
	{
		const auto kernel = GenerateKernel();
		SSAOParamsGpu initialParams = {};
		for (size_t i = 0; i < kMaxKernelSamples; ++i)
		{
			initialParams.kernelSamples[i] = kernel[i];
		}

		m_paramsUBO = std::make_unique<UniformBuffer<SSAOParamsGpu>>(
			device, physicalDevice, "SSAOParamsUBO");
		m_paramsUBO->Upload(initialParams);

		NEURUS_LOG("[SSAOPass] Created params UBO (" << sizeof(SSAOParamsGpu) << " bytes, "
		           << kMaxKernelSamples << " kernel samples)");
	}

	// --- Generate and upload noise rotation vectors ---
	{
		const auto noise = GenerateNoise();

		m_noiseUBO = std::make_unique<GPUBuffer>(
			device, physicalDevice,
			graphicsQueue, queueFamilyIndex,
			sizeof(noise),
			vk::BufferUsageFlagBits::eUniformBuffer,
			"SSAONoiseUBO");
		m_noiseUBO->Upload(noise.data(), sizeof(noise));

		NEURUS_LOG("[SSAOPass] Created noise UBO (" << sizeof(noise) << " bytes, "
		           << kNoiseEntryCount << " entries)");
	}

#ifdef _DEBUG
	for (uint32_t i = 0; i < numSets; ++i)
	{
		const std::string dsName = "SSAOPass_Set" + std::to_string(i);
		m_descriptorSets[i].SetDebugName(dsName.c_str());
	}
#endif
}

// ---------------------------------------------------------------------------

std::array<KernelSampleGpu, SSAOPass::kMaxKernelSamples> SSAOPass::GenerateKernel()
{
	DeterministicRNG rng(0xDEADBEEF);
	std::array<KernelSampleGpu, kMaxKernelSamples> kernel;

	// Generate hemisphere samples with increasing radius:
	//   scale = lerp(0.1, 1.0, (i / (N-1))²)
	// This clusters samples near the centre for fine detail and spreads
	// far samples for large-scale occlusion.
	for (uint32_t i = 0; i < kMaxKernelSamples; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(kMaxKernelSamples - 1);
		const float scale = 0.1f + 0.9f * t * t;

		const glm::vec3 dir = scale * rng.rand3nh();

		kernel[i].x   = dir.x;
		kernel[i].y   = dir.y;
		kernel[i].z   = dir.z;
		kernel[i]._pad = 0.0f;
	}

	NEURUS_LOG("[SSAOPass] Generated " << kMaxKernelSamples << " hemisphere kernel samples");
	return kernel;
}

// ---------------------------------------------------------------------------
// Noise generation
// ---------------------------------------------------------------------------

std::array<NoiseEntryGpu, SSAOPass::kNoiseEntryCount> SSAOPass::GenerateNoise()
{
	DeterministicRNG rng(0xCAFEBABE);
	std::array<NoiseEntryGpu, kNoiseEntryCount> noise;

	for (uint32_t i = 0; i < kNoiseEntryCount; ++i)
	{
		// Random unit direction — used to rotate the tangent plane
		const glm::vec3 dir = rng.rand3n();

		noise[i].x   = dir.x;
		noise[i].y   = dir.y;
		noise[i].z   = dir.z;
		noise[i]._pad = 0.0f;
	}

	NEURUS_LOG("[SSAOPass] Generated " << kNoiseEntryCount << " noise rotation entries");
	return noise;
}

// ---------------------------------------------------------------------------
// Descriptor set layout
// ---------------------------------------------------------------------------

DescriptorSetLayout SSAOPass::CreateDescriptorSetLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		// G-Buffer inputs (combined image samplers)
		.AddBinding(0,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(2,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// SSAO output (storage image)
		.AddBinding(3,
		            vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		// SSAO params UBO (camera + kernel)
		.AddBinding(4,
		            vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		// Noise UBO
		.AddBinding(5,
		            vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(device);
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

vk::raii::Pipeline SSAOPass::CreatePipeline(const vk::raii::Device& device,
                                             const uint32_t* compSpv,
                                             size_t compSize)
{
	// --- Create compute shader module from embedded SPIR-V ---
	auto compModule = ShaderModule::FromEmbedded(device, compSpv, compSize);

	// --- Push constant range (4 ints = 16 bytes) ---
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		4 * sizeof(int32_t));  // kernelLength, radius (float), noiseSize, frameIndex

	// --- Build compute pipeline ---
	return m_pipelineBuilder->SetShaderStage(compModule, "main")
		.AddDescriptorSetLayout(*m_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void SSAOPass::WriteDescriptors(uint32_t setIndex, vk::Extent2D extent, RenderCache& cache)
{
	DescriptorSet& dstSet = m_descriptorSets[setIndex];

	// --- Write G-Buffer input descriptors (combined image samplers) ---
	{
		const std::array<AttachmentName, 3> gBufferInputs = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,   // alpha stored in albedo.a or separate?
		};

		for (uint32_t i = 0; i < 3; ++i)
		{
			const auto& attachment = cache.GetAttachment(gBufferInputs[i], extent);

			vk::DescriptorImageInfo imageInfo(
				*m_sampler,                              // sampler
				*attachment.ImageViewHandle(),           // imageView
				vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
			);

			dstSet.WriteImage(i, imageInfo,
			                  vk::DescriptorType::eCombinedImageSampler);
		}
	}

	// --- Write SSAO output (storage image) ---
	{
		const auto& ssaoAtt = cache.GetAttachment(AttachmentName::SSAO, extent);

		vk::DescriptorImageInfo imageInfo(
			nullptr,                              // sampler (not used for storage images)
			*ssaoAtt.ImageViewHandle(),           // imageView
			vk::ImageLayout::eGeneral             // imageLayout
		);

		dstSet.WriteImage(3, imageInfo,
		                  vk::DescriptorType::eStorageImage);
	}

	// --- Write SSAO params UBO ---
	{
		dstSet.WriteBuffer(4, m_paramsUBO->GetDescriptorInfo(),
		                   vk::DescriptorType::eUniformBuffer);
	}

	// --- Write noise UBO ---
	{
		dstSet.WriteBuffer(5, m_noiseUBO->GetDescriptorInfo(),
		                   vk::DescriptorType::eUniformBuffer);
	}
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void SSAOPass::Record(vk::CommandBuffer cmdBuf, RenderCache& cache, const RenderContext& ctx)
{
	const vk::Extent2D renderExtent = ctx.renderExtent;
	const uint32_t    frameIndex   = ctx.frameIndex;

	// --- 0. Update per-frame SSAO params UBO (camera matrices) ---
	//     Maps the host-visible UBO and overwrites only the first 128 bytes
	//     (viewProj + view + cameraPos).  Kernel samples (offset 144) are
	//     written once in the constructor and left intact.
	if (m_paramsUBO)
	{
		void* mapped = m_paramsUBO->Map();
		auto* params = static_cast<SSAOParamsGpu*>(mapped);

		// View-projection matrix (column-major)
		const float* vp = &ctx.viewProj[0][0];
		for (int i = 0; i < 16; ++i) params->viewProj[i] = vp[i];

		// View matrix
		const float* vm = &ctx.view[0][0];
		for (int i = 0; i < 16; ++i) params->view[i] = vm[i];

		// Camera position
		params->camX = ctx.cameraPos.x;
		params->camY = ctx.cameraPos.y;
		params->camZ = ctx.cameraPos.z;
		params->camW = 0.0f;

		// Kernel samples are NOT touched — they remain from construction

		m_paramsUBO->Unmap();
	}

	// --- 1. Write descriptor set for this frame slot ---
	WriteDescriptors(frameIndex, renderExtent, cache);

	// --- 2. Transition G-Buffer images to ShaderRead and SSAO attachment to ShaderWrite ---
	{
		const std::array<AttachmentName, 3> gBufferInputs = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,
		};

		for (size_t i = 0; i < 3; ++i)
		{
			auto& attachment = cache.GetAttachment(gBufferInputs[i], renderExtent);
			Barrier::Transition(cmdBuf, attachment, ImageState::ColorShaderRead);
		}

		// SSAO attachment: current state → ShaderWrite (compute write)
		auto& ssaoAtt = cache.GetAttachment(AttachmentName::SSAO, renderExtent);
		Barrier::Transition(cmdBuf, ssaoAtt, ImageState::ShaderWrite);
	}

	// --- 3. Bind compute pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);

	// --- 4. Bind descriptor set ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          *m_pipelineBuilder->pipelineLayout(),
	                          0,                                    // firstSet
	                          {m_descriptorSets[frameIndex].handle()},
	                          {});

	// --- 5. Push constants ---
	{
		struct SSAOPushConstants
		{
			int32_t kernelLength;
			float   radius;
			int32_t noiseSize;
			int32_t frameIndex;
		};

		SSAOPushConstants pc = {};
		pc.kernelLength = m_kernelLength;
		pc.radius       = m_radius;
		pc.noiseSize    = m_noiseSize;
		pc.frameIndex   = static_cast<int32_t>(frameIndex);

		cmdBuf.pushConstants<SSAOPushConstants>(
			*m_pipelineBuilder->pipelineLayout(),
			vk::ShaderStageFlagBits::eCompute,
			0,
			pc);
	}

	// --- 6. Dispatch ---
	const uint32_t groupCountX = (renderExtent.width  + 15) / 16;
	const uint32_t groupCountY = (renderExtent.height + 15) / 16;
	cmdBuf.dispatch(groupCountX, groupCountY, 1);

	// --- 7. Transition SSAO output: General → ShaderRead for lighting pass ---
	{
		auto& ssaoAtt = cache.GetAttachment(AttachmentName::SSAO, renderExtent);
		Barrier::Transition(cmdBuf, ssaoAtt, ImageState::ColorShaderRead);
	}
}

} // namespace neurus
