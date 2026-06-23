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
	                float farPlane = kDefaultFarPlane);

	~ShadowDepthPass() override = default;
	ShadowDepthPass(const ShadowDepthPass&) = delete;
	ShadowDepthPass& operator=(const ShadowDepthPass&) = delete;
	ShadowDepthPass(ShadowDepthPass&&) noexcept = default;
	ShadowDepthPass& operator=(ShadowDepthPass&&) noexcept = default;

	void SetLightPosition(const glm::vec3& position);
	void Record(vk::CommandBuffer cmdBuf, const PassContext& ctx) override;

	void createDepthmap(const vk::raii::Device& device,
	                    const vk::raii::PhysicalDevice& physicalDevice);
	void createDepthViews(const vk::raii::Device& device);

	Image& ShadowCubemap() { return *m_cubemap; }
	Image& Depthmap() { return *m_depthmap; }
	const vk::raii::ImageView& DepthView() const { return m_depthView; }
	uint32_t Resolution() const { return m_resolution; }

private:
	static DescriptorSetLayout CreateLightLayout(const vk::raii::Device& device);

	// Must match std140 layout in shadow shaders:
	//   mat4 faceViewProj[6];  // 384 bytes (offset 0)
	//   vec3 lightWorldPos;    //  16 bytes (offset 384) — std140 pads vec3
	//   float farPlane;        //   4 bytes (offset 400)
	// Total: 404 bytes
	struct LightUBO { glm::mat4 faceVP[6]; float lpx, lpy, lpz; float _pad0; float farPlane; };
	static_assert(sizeof(LightUBO) == 404,
	              "LightUBO size must match std140 layout (vec3 padding)");

	void createDepthCubemap(const vk::raii::Device& device,
	                        const vk::raii::PhysicalDevice& physicalDevice);
	void createFaceViews(const vk::raii::Device& device);
	void createUniforms(const vk::raii::Device& device,
	                    const vk::raii::PhysicalDevice& physicalDevice,
	                    vk::Queue queue, uint32_t qfi);
	void createPipeline(const vk::raii::Device& device);
	void updateUBO();

	// --- Parameters ---
	uint32_t m_resolution;
	float m_farPlane;
	glm::vec3 m_lightPosition{0.0f};

	// --- GPU resources ---
	std::unique_ptr<Image> m_cubemap;
	std::unique_ptr<Image> m_depthmap;
	std::vector<vk::raii::ImageView> m_faceViews;
	vk::raii::ImageView m_depthView = nullptr;
	std::unique_ptr<VulkanBuffer> m_ubo;
	DescriptorSetLayout m_layout;
	DescriptorPool m_pool;
	std::unique_ptr<DescriptorSet> m_set;
	BufferLayout m_vtxLayout;
	vk::raii::PipelineLayout m_pipelineLayout = nullptr;
	vk::raii::Pipeline m_pipeline = nullptr;
};

} // namespace neurus
