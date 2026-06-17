/**
 * @file LightingPass.cpp
 * @brief PBR lighting pass implementation.
 */

#include "LightingPass.h"

#include "AttachmentManager.h"
#include "ComputePipelineBuilder.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "shaders/ShaderModule.h"

#include "Log.h"

#include <array>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LightingPass::LightingPass(const vk::raii::Device& device,
                           const vk::raii::PhysicalDevice& physicalDevice,
                           AttachmentManager& attachmentManager,
                           uint32_t numSets,
                           const uint32_t* compSpv,
                           size_t compSize)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
	, m_attachmentManager(&attachmentManager)
	// --- Nearest-neighbour sampler for G-Buffer reads ---
	, m_sampler(CreateSampler(device, physicalDevice))
	// --- Descriptor set layout ---
	, m_descriptorSetLayout(CreateDescriptorSetLayout(device))
	// --- Descriptor pool (numSets sets, pool sizes from layout × numSets) ---
	, m_descriptorPool(device,
	                   numSets,
	                   DescriptorPool::CalculatePoolSizes({&m_descriptorSetLayout}, numSets))
	// --- Descriptor sets (one per in-flight frame) ---
	, m_descriptorSets(m_descriptorPool.Allocate(m_descriptorSetLayout, numSets))
	// --- Pipeline builder (must outlive pipeline) ---
	, m_pipelineBuilder(std::make_unique<ComputePipelineBuilder>(device))
	, m_pipeline(CreatePipeline(device, compSpv, compSize))
{
	NEURUS_LOG("[LightingPass] compSize=" << compSize << " numSets=" << numSets);
}

LightingPass::~LightingPass() = default;

DescriptorSetLayout LightingPass::CreateDescriptorSetLayout(const vk::raii::Device& device)
{
	auto bindings = BuildLayout()
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
		.AddBinding(3,
		            vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		// Output HDR colour (storage image)
		.AddBinding(4,
		            vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		// Light SSBO
		.AddBinding(5,
		            vk::DescriptorType::eStorageBuffer,
		            vk::ShaderStageFlagBits::eCompute)
		.Build();

	return DescriptorSetLayout(device, bindings);
}

// ---------------------------------------------------------------------------
// Sampler factory
// ---------------------------------------------------------------------------

vk::raii::Sampler LightingPass::CreateSampler(const vk::raii::Device& device,
                                               const vk::raii::PhysicalDevice& physicalDevice)
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

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------

vk::raii::Pipeline LightingPass::CreatePipeline(const vk::raii::Device& device,
                                                 const uint32_t* compSpv,
                                                 size_t compSize)
{
	// --- Create compute shader module from embedded SPIR-V ---
	auto compModule = ShaderModule::FromEmbedded(device, compSpv, compSize);

	// --- Push constant range ---
	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute,
		0,
		sizeof(LightingPushConstants));

	// --- Build compute pipeline ---
	return m_pipelineBuilder->SetShaderStage(compModule, "main")
		.AddDescriptorSetLayout(*m_descriptorSetLayout.layout())
		.AddPushConstantRange(pushRange)
		.BuildComputePipeline();
}

// ---------------------------------------------------------------------------
// Descriptor writes
// ---------------------------------------------------------------------------

void LightingPass::WriteDescriptors(uint32_t setIndex, const VulkanBuffer& lightSSBO)
{
	DescriptorSet& dstSet = m_descriptorSets[setIndex];

	const std::array<AttachmentName, 4> gBufferInputs = {
		AttachmentName::Position,
		AttachmentName::Normal,
		AttachmentName::Albedo,
		AttachmentName::MetallicRoughness,
	};

	// --- Write G-Buffer input descriptors (combined image samplers) ---
	for (uint32_t i = 0; i < 4; ++i)
	{
		const auto& attachment = m_attachmentManager->GetAttachment(gBufferInputs[i]);

		vk::DescriptorImageInfo imageInfo(
			*m_sampler,                              // sampler
			*attachment.ImageViewHandle(),           // imageView
			vk::ImageLayout::eShaderReadOnlyOptimal  // imageLayout
		);

		dstSet.WriteImage(i, imageInfo,
		                  vk::DescriptorType::eCombinedImageSampler);
	}

	// --- Write HDR colour output (storage image) ---
	{
		const auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);

		vk::DescriptorImageInfo imageInfo(
			nullptr,                              // sampler (not used for storage images)
			*hdrColor.ImageViewHandle(),          // imageView
			vk::ImageLayout::eGeneral             // imageLayout
		);

		dstSet.WriteImage(4, imageInfo,
		                  vk::DescriptorType::eStorageImage);
	}

	// --- Write light SSBO ---
	{
		dstSet.WriteBuffer(5, lightSSBO.GetDescriptorInfo(),
		                   vk::DescriptorType::eStorageBuffer);
	}
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

void LightingPass::Record(vk::CommandBuffer cmdBuf,
                          const VulkanBuffer& lightSSBO,
                          uint32_t lightCount,
                          const glm::vec3& cameraPos,
                          const glm::mat4& viewMatrix,
                          vk::Extent2D renderExtent,
                          uint32_t frameIndex)
{
	// --- 1. Write descriptor set for this frame slot ---
	WriteDescriptors(frameIndex, lightSSBO);

	// --- 2. Transition G-Buffer images to SHADER_READ_ONLY_OPTIMAL ---
	//     and HDRColor to GENERAL for compute write.
	//     Uses pipelineBarrier2 (synchronization2) for correct layout
	//     tracking with VK_KHR_dynamic_rendering.
	{
		const std::array<AttachmentName, 4> gBufferInputs = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,
			AttachmentName::MetallicRoughness,
		};

		std::array<vk::ImageMemoryBarrier2, 5> barriers;

		for (size_t i = 0; i < 4; ++i)
		{
			auto& attachment = m_attachmentManager->GetAttachment(gBufferInputs[i]);
			barriers[i] = vk::ImageMemoryBarrier2(
				vk::PipelineStageFlagBits2::eColorAttachmentOutput,  // srcStage
				vk::AccessFlagBits2::eColorAttachmentWrite,           // srcAccess
				vk::PipelineStageFlagBits2::eComputeShader,            // dstStage
				vk::AccessFlagBits2::eShaderRead,                      // dstAccess
				vk::ImageLayout::eColorAttachmentOptimal,              // oldLayout
				vk::ImageLayout::eShaderReadOnlyOptimal,               // newLayout
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				*attachment.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          0, 1, 0, 1));

			// Update CPU-side layout tracking
			attachment.SetCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
		}

		// HDRColor: current layout → GENERAL
		auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
		const vk::ImageLayout hdrOldLayout = hdrColor.CurrentLayout();
		barriers[4] = vk::ImageMemoryBarrier2(
			(hdrOldLayout == vk::ImageLayout::eUndefined)
				? vk::PipelineStageFlagBits2::eTopOfPipe
				: vk::PipelineStageFlagBits2::eAllCommands,            // srcStage
			(hdrOldLayout == vk::ImageLayout::eUndefined)
				? vk::AccessFlagBits2::eNone
				: vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,  // srcAccess
			vk::PipelineStageFlagBits2::eComputeShader,             // dstStage
			vk::AccessFlagBits2::eShaderWrite,                      // dstAccess
			hdrOldLayout,                                            // oldLayout — tracked, correct
			vk::ImageLayout::eGeneral,                              // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*hdrColor.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));

		// Update CPU-side layout tracking (the barrier did this implicitly on GPU)
		hdrColor.SetCurrentLayout(vk::ImageLayout::eGeneral);

		const vk::DependencyInfo depInfo({}, {}, {}, barriers);
		cmdBuf.pipelineBarrier2(depInfo);
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
		LightingPushConstants pc = {};
		pc.lightCount = static_cast<int32_t>(lightCount);
		pc.camX = cameraPos.x;
		pc.camY = cameraPos.y;
		pc.camZ = cameraPos.z;
		// Copy view matrix (column-major, same as GLSL)
		const float* vm = &viewMatrix[0][0];
		for (int i = 0; i < 16; ++i)
		{
			pc.view[i] = vm[i];
		}

		cmdBuf.pushConstants<LightingPushConstants>(
			*m_pipelineBuilder->pipelineLayout(),
			vk::ShaderStageFlagBits::eCompute,
			0,
			pc);
	}

	// --- 6. Dispatch ---
	const uint32_t groupCountX = (renderExtent.width  + 15) / 16;
	const uint32_t groupCountY = (renderExtent.height + 15) / 16;
	cmdBuf.dispatch(groupCountX, groupCountY, 1);

	// --- 7. Memory barrier: make HDR output visible for subsequent passes ---
	//     Uses pipelineBarrier2 for consistent layout tracking.
	{
		const auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
		const vk::ImageMemoryBarrier2 barrier(
			vk::PipelineStageFlagBits2::eComputeShader,          // srcStage
			vk::AccessFlagBits2::eShaderWrite,                    // srcAccess
			vk::PipelineStageFlagBits2::eComputeShader,            // dstStage
			vk::AccessFlagBits2::eShaderRead,                      // dstAccess
			vk::ImageLayout::eGeneral,                             // oldLayout
			vk::ImageLayout::eGeneral,                             // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*hdrColor.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));

		const vk::DependencyInfo depInfo({}, {}, {}, barrier);
		cmdBuf.pipelineBarrier2(depInfo);
	}
}

} // namespace neurus
