/**
 * @file ShadowEvalPass.h
 * @brief Point light shadow evaluation compute pass.
 *
 * Inherits ComputePass for descriptor pool/set infrastructure.
 *
 * Descriptor bindings:
 *   0: G-Buffer world position (combined image sampler)
 *   1: Shadow depth cubemap        (combined image sampler)
 *   2: Shadow intensity output     (storage image, R8)
 */

#pragma once

#include "ComputePass.h"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace neurus {

class Image;
class ComputePipelineBuilder;
struct GeometryRenderItem;

class ShadowEvalPass : public ComputePass
{
public:
	static constexpr float kDefaultBias = 0.005f;

	ShadowEvalPass(const vk::raii::Device& device,
	               const vk::raii::PhysicalDevice& physicalDevice,
	               AttachmentManager* attachmentManager,
	               uint32_t numSets);

	~ShadowEvalPass() override = default;
	ShadowEvalPass(ShadowEvalPass&&) noexcept = default;
	ShadowEvalPass& operator=(ShadowEvalPass&&) noexcept = default;

	void SetLight(const Image& cubemap,
	              const glm::vec3& lightPos,
	              float farPlane,
	              float bias = kDefaultBias);

	void WriteDescriptors(uint32_t setIndex) override;
	void Record(vk::CommandBuffer cmdBuf, const PassContext& ctx) override;

private:
	static DescriptorSetLayout CreateDescriptorLayout(const vk::raii::Device& device);

	struct PushConstants
	{
		float lightPosX, lightPosY, lightPosZ;
		float farPlane;
		float bias;
	};

	const Image* m_shadowCubemap = nullptr;
	glm::vec3 m_lightPosition{0.0f};
	float m_farPlane = 25.0f;
	float m_bias = kDefaultBias;

	vk::raii::Pipeline m_pipeline = nullptr;
};

} // namespace neurus
