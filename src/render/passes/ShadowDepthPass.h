/**
 * @file ShadowDepthPass.h
 * @brief Point-light shadow depth cubemap pass.
 */

#pragma once

#include "passes/Pass.h"
#include "passes/PassContext.h"
#include "../Image.h"
#include "../VulkanBuffer.h"
#include "../DescriptorManager.h"
#include "../buffers/BufferLayout.h"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace neurus {

struct GeometryRenderItem;

constexpr uint32_t kShadowFaceCount = 6;

/** @brief Shadow depth pass rendering mode. */
enum class ShadowMode
{
	SingleFace,  ///< 6 sequential passes, one per cubemap face (default)
	Multiview    ///< Single pass via VK_KHR_multiview, all 6 faces at once
};

class ShadowDepthPass : public Pass
{
public:
	static constexpr uint32_t kDefaultResolution = 1024;
	static constexpr float    kDefaultFarPlane   = 25.0f;

	ShadowDepthPass(const vk::raii::Device& device,
	                const vk::raii::PhysicalDevice& physicalDevice,
	                vk::Queue graphicsQueue,
	                uint32_t queueFamilyIndex,
	                uint32_t resolution = kDefaultResolution,
	                float farPlane = kDefaultFarPlane,
	                ShadowMode mode = ShadowMode::SingleFace);

	~ShadowDepthPass() override = default;
	ShadowDepthPass(const ShadowDepthPass&) = delete;
	ShadowDepthPass& operator=(const ShadowDepthPass&) = delete;
	ShadowDepthPass(ShadowDepthPass&&) noexcept = default;
	ShadowDepthPass& operator=(ShadowDepthPass&&) noexcept = default;

	void SetLightPosition(const glm::vec3& position);
	void Record(vk::CommandBuffer cmdBuf, const PassContext& ctx) override;

	void createDepthmap(const vk::raii::Device& device,
	                    const vk::raii::PhysicalDevice& physicalDevice);

	// --- Accessors ---
	ShadowMode Mode() const { return m_mode; }
	Image& ShadowCubemap() { return *m_cubemap; }
	Image& Depthmap() { return *m_depthmap; }
	uint32_t Resolution() const { return m_resolution; }
	const BufferLayout& VertexLayout() const { return m_vtxLayout; }
	const DescriptorSetLayout& LightDescriptorLayout() const { return m_layout; }
	const DescriptorSetLayout& GetLightLayout() const { return m_layout; }
	vk::DescriptorSet GetLightSetHandle() const { return m_set->handle(); }
	VulkanBuffer& GetUBO() { return *m_ubo; }
	const vk::raii::Pipeline& GetPipeline() const
	{
		return m_mode == ShadowMode::Multiview ? m_multiviewPipeline : m_pipeline;
	}
	const vk::raii::PipelineLayout& GetPipelineLayout() const
	{
		return m_mode == ShadowMode::Multiview ? m_multiviewPipelineLayout : m_pipelineLayout;
	}

	// Must match std140 layout in shadow shaders:
	//   mat4 faceViewProj[6];  // 384 bytes (offset 0)
	//   vec3 lightWorldPos;    //  12 bytes (offset 384)
	//   float farPlane;        //   4 bytes (offset 396)
	// Total: 400 bytes
	struct LightUBO { glm::mat4 faceVP[6]; float lpx, lpy, lpz; float farPlane; };
	static_assert(sizeof(LightUBO) == 400,
	              "LightUBO size must match std140 layout");

private:
	static DescriptorSetLayout CreateLightLayout(const vk::raii::Device& device);

	void createDepthCubemap(const vk::raii::Device& device,
	                        const vk::raii::PhysicalDevice& physicalDevice);
	void createUniforms(const vk::raii::Device& device,
	                    const vk::raii::PhysicalDevice& physicalDevice,
	                    vk::Queue queue, uint32_t qfi);
	void createSingleFacePipeline(const vk::raii::Device& device);
	void createMultiviewPipeline(const vk::raii::Device& device);
	void createMultiviewColorPipeline(const vk::raii::Device& device);
	void updateUBO();

	// --- Parameters ---
	uint32_t m_resolution;
	float m_farPlane;
	glm::vec3 m_lightPosition{0.0f};
	ShadowMode m_mode = ShadowMode::SingleFace;

	// --- GPU resources ---
	std::unique_ptr<Image> m_cubemap;
	std::unique_ptr<Image> m_depthmap;
	std::unique_ptr<VulkanBuffer> m_ubo;
	DescriptorSetLayout m_layout;
	DescriptorPool m_pool;
	std::unique_ptr<DescriptorSet> m_set;
	BufferLayout m_vtxLayout;
	vk::raii::PipelineLayout m_pipelineLayout = nullptr;
	vk::raii::Pipeline m_pipeline = nullptr;

	// Multiview resources (only allocated when mode == Multiview)
	vk::raii::PipelineLayout m_multiviewPipelineLayout = nullptr;
	vk::raii::Pipeline m_multiviewPipeline = nullptr;

	// Multiview colour+depth pipeline (for optional colour-output verification)
	vk::raii::PipelineLayout m_multiviewColorPipelineLayout = nullptr;
	vk::raii::Pipeline m_multiviewColorPipeline = nullptr;
};

} // namespace neurus
