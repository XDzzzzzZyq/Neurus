/**
 * @file ShadowEvalPass.cpp
 * @brief Point light shadow evaluation compute pass implementation.
 */

#include "passes/ShadowEvalPass.h"
#include "passes/AttachmentManager.h"
#include "passes/PassContext.h"
#include "../ComputePipelineBuilder.h"
#include "../DescriptorManager.h"
#include "../shaders/ShaderModule.h"

#include "shadow_eval.comp.h"

#include "Log.h"

namespace neurus {

// ===========================================================================
// Descriptor set layout
// ===========================================================================

DescriptorSetLayout ShadowEvalPass::CreateDescriptorLayout(const vk::raii::Device& device)
{
	return BuildLayout()
		.AddBinding(0, vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(1, vk::DescriptorType::eCombinedImageSampler,
		            vk::ShaderStageFlagBits::eCompute)
		.AddBinding(2, vk::DescriptorType::eStorageImage,
		            vk::ShaderStageFlagBits::eCompute)
		.Build(device);
}

// ===========================================================================
// Constructor
// ===========================================================================

ShadowEvalPass::ShadowEvalPass(const vk::raii::Device& device,
                                const vk::raii::PhysicalDevice& physicalDevice,
                                AttachmentManager* attachmentManager,
                                uint32_t numSets)
	: ComputePass(device, physicalDevice, attachmentManager,
	              CreateDescriptorLayout(device), numSets)
{
	auto compModule = ShaderModule::FromEmbedded(
		device, shadow_eval_comp_spv, sizeof(shadow_eval_comp_spv));

	vk::PushConstantRange pushRange(
		vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants));

	m_pipelineBuilder->SetShaderStage(std::move(compModule), "main");
	m_pipelineBuilder->AddDescriptorSetLayout(*m_descriptorSetLayout.layout());
	m_pipelineBuilder->AddPushConstantRange(pushRange);

	m_pipeline = m_pipelineBuilder->BuildComputePipeline();

	NEURUS_LOG("[ShadowEvalPass] Created, " << numSets << " descriptor sets");
}

// ===========================================================================
// SetLight
// ===========================================================================

void ShadowEvalPass::SetLight(const Image& cubemap,
                               const glm::vec3& lightPos,
                               float farPlane,
                               float bias)
{
	m_shadowCubemap = &cubemap;
	m_lightPosition = lightPos;
	m_farPlane = farPlane;
	m_bias = bias;

	NEURUS_LOG("[ShadowEvalPass] SetLight: pos=(" << lightPos.x << "," << lightPos.y << "," << lightPos.z
	           << ") farPlane=" << farPlane << " bias=" << bias);
}

// ===========================================================================
// WriteDescriptors
// ===========================================================================

void ShadowEvalPass::WriteDescriptors(uint32_t setIndex)
{
	if (setIndex >= m_descriptorSets.size()) return;
	if (!m_attachmentManager || !m_shadowCubemap) return;

	auto& ds = m_descriptorSets[setIndex];

	// Binding 0: G-Buffer world position
	{
		auto& posAtt = m_attachmentManager->GetAttachment(AttachmentName::Position);
		vk::DescriptorImageInfo posInfo(*m_sampler, *posAtt.ImageViewHandle(),
		                                vk::ImageLayout::eShaderReadOnlyOptimal);
		ds.WriteImage(0, posInfo, vk::DescriptorType::eCombinedImageSampler);
	}

	// Binding 1: Shadow depth cubemap
	{
		vk::DescriptorImageInfo shadowInfo(*m_sampler, *m_shadowCubemap->ImageViewHandle(),
		                                    vk::ImageLayout::eShaderReadOnlyOptimal);
		ds.WriteImage(1, shadowInfo, vk::DescriptorType::eCombinedImageSampler);
	}

	// Binding 2: Shadow intensity output (storage image)
	{
		auto& shadowAtt = m_attachmentManager->GetAttachment(AttachmentName::ShadowIntensity);
		vk::DescriptorImageInfo outInfo(nullptr, *shadowAtt.ImageViewHandle(),
		                                 vk::ImageLayout::eGeneral);
		ds.WriteImage(2, outInfo, vk::DescriptorType::eStorageImage);
	}
}

// ===========================================================================
// Record
// ===========================================================================

void ShadowEvalPass::Record(vk::CommandBuffer cmdBuf, const PassContext& ctx)
{
	if (!m_attachmentManager || !m_shadowCubemap)
	{
		NEURUS_ERR("[ShadowEvalPass] Record() called without SetLight()");
		return;
	}

	const uint32_t frameIndex = ctx.frameIndex;
	WriteDescriptors(frameIndex);

	NEURUS_LOG("[ShadowEvalPass] Record: extent=" << ctx.renderExtent.width << "x"
	           << ctx.renderExtent.height << " frameIndex=" << frameIndex
	           << " lightPos=(" << m_lightPosition.x << "," << m_lightPosition.y << "," << m_lightPosition.z << ")"
	           << " farPlane=" << m_farPlane << " bias=" << m_bias);

	// Transition G-Buffer position to read
	TransitionGbufferToRead(cmdBuf,
	                        std::array<AttachmentName, 1>{AttachmentName::Position},
	                        *m_attachmentManager);

	// Ensure shadow cubemap is readable
	{
		vk::ImageMemoryBarrier2 barrier(
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderRead,
			vk::PipelineStageFlagBits2::eComputeShader,
			vk::AccessFlagBits2::eShaderRead,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*m_shadowCubemap->ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 6));
		const vk::DependencyInfo depInfo({}, {}, {}, barrier);
		cmdBuf.pipelineBarrier2(depInfo);
	}

	// Transition shadow intensity output to GENERAL
	TransitionOutputToGeneral(cmdBuf, AttachmentName::ShadowIntensity,
	                          *m_attachmentManager);

	// Push constants
	PushConstants pc{};
	pc.lightPosX = m_lightPosition.x;
	pc.lightPosY = m_lightPosition.y;
	pc.lightPosZ = m_lightPosition.z;
	pc.farPlane = m_farPlane;
	pc.bias = m_bias;

	// Dispatch
	DispatchCompute(cmdBuf, ctx.renderExtent, m_pipeline,
	                *m_pipelineBuilder->pipelineLayout(),
	                m_descriptorSets[frameIndex].handle(),
	                &pc, sizeof(PushConstants));
}

} // namespace neurus
