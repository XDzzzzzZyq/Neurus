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

#include <array>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LightingPass::LightingPass(const vk::raii::Device& device,
                           const vk::raii::PhysicalDevice& physicalDevice,
                           AttachmentManager& attachmentManager,
                           const uint32_t* compSpv,
                           size_t compSize)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
	, m_attachmentManager(&attachmentManager)
	// --- Nearest-neighbour sampler for G-Buffer reads ---
	, m_sampler(CreateSampler(device, physicalDevice))
	// --- Descriptor set layout ---
	, m_descriptorSetLayout(CreateDescriptorSetLayout(device))
	// --- Descriptor pool (1 set, pool sizes from layout) ---
	, m_descriptorPool(device,
	                   1,
	                   DescriptorPool::CalculatePoolSizes({&m_descriptorSetLayout}, 1))
	// --- Descriptor set ---
	, m_descriptorSet(std::move(
	      m_descriptorPool.Allocate(m_descriptorSetLayout, 1).front()))
	// --- Pipeline builder (must outlive pipeline) ---
	, m_pipelineBuilder(std::make_unique<ComputePipelineBuilder>(device))
	, m_pipeline(CreatePipeline(device, compSpv, compSize))
{
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

void LightingPass::WriteDescriptors(const VulkanBuffer& lightSSBO)
{
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

		m_descriptorSet.WriteImage(i, imageInfo,
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

		m_descriptorSet.WriteImage(4, imageInfo,
		                           vk::DescriptorType::eStorageImage);
	}

	// --- Write light SSBO ---
	{
		m_descriptorSet.WriteBuffer(5, lightSSBO.GetDescriptorInfo(),
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
                          vk::Extent2D renderExtent)
{
	// --- 1. Write descriptor set (binds image views + SSBO) ---
	WriteDescriptors(lightSSBO);

	// --- 2. Transition G-Buffer images to SHADER_READ_ONLY_OPTIMAL ---
	//     and HDRColor to GENERAL for compute write.
	{
		const std::array<AttachmentName, 4> gBufferInputs = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,
			AttachmentName::MetallicRoughness,
		};

		std::array<vk::ImageMemoryBarrier, 5> barriers;

		for (size_t i = 0; i < 4; ++i)
		{
			const auto& attachment = m_attachmentManager->GetAttachment(gBufferInputs[i]);
			barriers[i] = vk::ImageMemoryBarrier(
				vk::AccessFlagBits::eColorAttachmentWrite,      // srcAccessMask
				vk::AccessFlagBits::eShaderRead,                 // dstAccessMask
				vk::ImageLayout::eColorAttachmentOptimal,        // oldLayout
				vk::ImageLayout::eShaderReadOnlyOptimal,         // newLayout
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				*attachment.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          0, 1, 0, 1));
		}

		// HDRColor: UNDEFINED → GENERAL
		const auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
		barriers[4] = vk::ImageMemoryBarrier(
			vk::AccessFlagBits::eNone,                         // srcAccessMask
			vk::AccessFlagBits::eShaderWrite,                   // dstAccessMask
			vk::ImageLayout::eUndefined,                        // oldLayout
			vk::ImageLayout::eGeneral,                          // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*hdrColor.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));

		cmdBuf.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput,  // srcStage
			vk::PipelineStageFlagBits::eComputeShader,           // dstStage
			{},
			{},
			{},
			barriers);
	}

	// --- 3. Bind compute pipeline ---
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, *m_pipeline);

	// --- 4. Bind descriptor set ---
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
	                          *m_pipelineBuilder->pipelineLayout(),
	                          0,                                    // firstSet
	                          {m_descriptorSet.handle()},
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
	{
		const auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
		vk::ImageMemoryBarrier barrier(
			vk::AccessFlagBits::eShaderWrite,                    // srcAccessMask
			vk::AccessFlagBits::eShaderRead,                      // dstAccessMask
			vk::ImageLayout::eGeneral,                            // oldLayout
			vk::ImageLayout::eGeneral,                            // newLayout
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			*hdrColor.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
			                          0, 1, 0, 1));

		cmdBuf.pipelineBarrier(
			vk::PipelineStageFlagBits::eComputeShader,
			vk::PipelineStageFlagBits::eComputeShader,
			{},
			{},
			{},
			{barrier});
	}
}

} // namespace neurus
