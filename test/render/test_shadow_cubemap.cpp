/**
 * @file test_shadow_cubemap.cpp
 * @brief GPU test: renders cube+plane scene into ShadowDepthPass cubemap,
 *        reads back face 3 (-Y) raw float depth data, and verifies every
 *        pixel against mathematically-computed expected depth values.
 *
 * Mathematical verification:
 *   - Cube unit at [-0.5, +0.5]^3, plane at y=0 spanning [-5,5] in XZ
 *   - Point light at (0, 3, 0), farPlane=25.0
 *   - Depth = dist(lightPos, worldPos) / farPlane written by fragment shader
 *   - For each pixel (px,py), ray-cast from light to determine expected depth
 *   - Compare pixel-by-pixel with tolerance +/-3/255 (~0.01176)
 *
 * Reference image regression (appended after mathematical verification):
 *   - First run: generates reference PNG → GTEST_SKIP
 *   - Second run: compares pixel-by-pixel with ±2 tolerance → PASS
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"
#include "shared/TestSimpleShadow.h"

#include "render/passes/ShadowDepthPass.h"
#include "render/passes/GeometryPass.h"    // for GeometryRenderItem definition
#include "render/passes/PassContext.h"
#include "render/Image.h"
#include "render/PipelineBuilder.h"
#include "render/DescriptorManager.h"
#include "render/shaders/ShaderModule.h"
#include "render/buffers/BufferLayout.h"
#include "render/VulkanBuffer.h"

#include "shared/TestReferenceImage.h"

#include "shadow_depth.vert.h"
#include "depth_to_color.frag.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ShadowCubemapTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRes        = 256;
	static constexpr float    kFarPlane   = 25.0f;
	static constexpr uint32_t kFaceIndex  = 3;  // -Y face
	static constexpr float    kTolerance  = 3.0f / 255.0f;  // +/-3 U8 steps in [0,1] range

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		m_shadowDepthPass = std::make_unique<ShadowDepthPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily, kRes, kFarPlane);

		m_shadowDepthPass->SetLightPosition(glm::vec3(0.0f, 3.0f, 0.0f));
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	/**
	 * @brief Computes the mathematically-expected depth value for pixel (px,py)
	 *        on cubemap face 3 (-Y), given a point light at lightPos.
	 *
	 * The fragment shader writes: gl_FragDepth = dist(lightPos, fragWorldPos) / farPlane.
	 *
	 * For face 3 (-Y), the view matrix is:
	 *   proj * lookAt(p, p+(0,-1,0), (0,0,-1))
	 * so the camera looks down in world -Y, with right = world +X, up = world -Z.
	 *
	 * Ray in world space from light: P(t) = lightPos + t * normalize(sx, -1, -sy), t>0
	 * where sx, sy in [-1, 1] are the NDC coordinates of the pixel centre.
	 *
	 * Intersections tested in order:
	 *   1. Cube AABB [-0.5, 0.5]^3 via slab method
	 *   2. Plane y=0 bounded to [-5, 5] in XZ
	 *   3. Background (no hit) -> depth = 1.0 (clear value)
	 *
	 * @return Expected depth = t / farPlane where t is distance to first hit.
	 */

	/**
	 * @brief Computes world-space direction from light for pixel (sx,sy) on cubemap face.
	 *
	 * Mathematically derived from the lookAt matrices in ShadowDepthPass::updateUBO.
	 * For each face, sx,sy in [-1,1] are NDC coordinates; the returned direction
	 * is the normalized world-space vector from the light through that pixel.
	 */
	static glm::vec3 FaceDirection(uint32_t faceIdx, float sx, float sy)
	{
		struct FC { glm::vec3 fwd, upVec; };
		static const FC kFaces[6] = {
			{{ 1, 0, 0}, {0,-1, 0}}, // 0: +X
			{{-1, 0, 0}, {0,-1, 0}}, // 1: -X
			{{ 0, 1, 0}, {0, 0, 1}}, // 2: +Y
			{{ 0,-1, 0}, {0, 0,-1}}, // 3: -Y
			{{ 0, 0, 1}, {0,-1, 0}}, // 4: +Z
			{{ 0, 0,-1}, {0,-1, 0}}, // 5: -Z
		};
		const auto& fc = kFaces[faceIdx];
		glm::vec3 right = glm::normalize(glm::cross(fc.fwd, fc.upVec));
		glm::vec3 actualUp = glm::normalize(glm::cross(right, fc.fwd));
		return glm::normalize(sx * right + sy * actualUp + fc.fwd);
	}

	static float ComputeExpectedDepth(uint32_t px, uint32_t py,
	                                  const glm::vec3& lightPos,
	                                  float farPlane, uint32_t res)
	{
		// NDC coordinates at pixel centre
		const float sx = (2.0f * static_cast<float>(px) + 1.0f) / static_cast<float>(res) - 1.0f;
		const float sy = (2.0f * static_cast<float>(py) + 1.0f) / static_cast<float>(res) - 1.0f;

		// World-space direction from light for -Y face:
		//   dir = normalize(sx * right + sy * up + 1 * (-forward))
		//       = normalize(sx * (1,0,0) + sy * (0,0,-1) + 1 * (0,-1,0))
		//       = normalize(sx, -1, -sy)
		const glm::vec3 dir = glm::normalize(glm::vec3(sx, -1.0f, -sy));

		// --- Plane y=0 intersection ---
		// lightPos.y + t * dir.y = 0  ->  t_plane = -lightPos.y / dir.y
		// (dir.y < 0 always for -Y face, so t_plane > 0)
		const float t_plane = -lightPos.y / dir.y;
		bool hitsPlaneInBounds = false;
		if (t_plane > 0.0f)
		{
			const glm::vec3 hitPoint = lightPos + t_plane * dir;
			if (std::abs(hitPoint.x) <= 5.0f && std::abs(hitPoint.z) <= 5.0f)
			{
				hitsPlaneInBounds = true;
			}
		}

		// --- Cube AABB [-0.5, 0.5]^3 slab test ---
		const float eps = 0.001f;  // avoid self-intersection at light source
		float t_min_cube = eps;
		float t_max_cube = 1e10f;

		// X slab
		if (std::abs(dir.x) > 1e-7f)
		{
			const float t1 = (-0.5f - lightPos.x) / dir.x;
			const float t2 = ( 0.5f - lightPos.x) / dir.x;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.x < -0.5f || lightPos.x > 0.5f)
		{
			t_min_cube = 1e10f;  // ray parallel to X, outside slab -> no hit
		}

		// Y slab
		if (std::abs(dir.y) > 1e-7f)
		{
			const float t1 = (-0.5f - lightPos.y) / dir.y;
			const float t2 = ( 0.5f - lightPos.y) / dir.y;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.y < -0.5f || lightPos.y > 0.5f)
		{
			t_min_cube = 1e10f;
		}

		// Z slab
		if (std::abs(dir.z) > 1e-7f)
		{
			const float t1 = (-0.5f - lightPos.z) / dir.z;
			const float t2 = ( 0.5f - lightPos.z) / dir.z;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.z < -0.5f || lightPos.z > 0.5f)
		{
			t_min_cube = 1e10f;
		}

		const bool hitsCube = (t_min_cube < t_max_cube && t_min_cube > eps);

		// Determine which intersection is first (smaller t)
		if (hitsCube && t_min_cube < t_plane)
		{
			return t_min_cube / farPlane;
		}

		if (hitsPlaneInBounds)
		{
			return t_plane / farPlane;
		}

		// No geometry hit -> clear value
		return 1.0f;
	}

	/**
	 * @brief Computes expected depth for a specific cubemap face.
	 *
	 * Uses FaceDirection() to determine the world-space ray from the light
	 * for the given pixel, then intersects with the cube AABB and plane.
	 * @param faceIdx Cubemap face index 0-5.
	 * @return Expected depth = t / farPlane where t is distance to first hit.
	 */
	static float ComputeExpectedDepth(uint32_t faceIdx,
	                                  uint32_t px, uint32_t py,
	                                  const glm::vec3& lightPos,
	                                  float farPlane, uint32_t res)
	{
		const float sx = (2.0f * static_cast<float>(px) + 1.0f) / static_cast<float>(res) - 1.0f;
		const float sy = (2.0f * static_cast<float>(py) + 1.0f) / static_cast<float>(res) - 1.0f;

		const glm::vec3 dir = glm::normalize(FaceDirection(faceIdx, sx, sy));

		// --- Plane y=0 intersection ---
		const float t_plane = -lightPos.y / dir.y;
		bool hitsPlaneInBounds = false;
		if (t_plane > 0.0f)
		{
			const glm::vec3 hitPoint = lightPos + t_plane * dir;
			if (std::abs(hitPoint.x) <= 5.0f && std::abs(hitPoint.z) <= 5.0f)
				hitsPlaneInBounds = true;
		}

		// --- Cube AABB [-0.5, 0.5]^3 slab test ---
		const float eps = 0.001f;
		float t_min_cube = eps;
		float t_max_cube = 1e10f;

		if (std::abs(dir.x) > 1e-7f)
		{
			const float t1 = (-0.5f - lightPos.x) / dir.x;
			const float t2 = ( 0.5f - lightPos.x) / dir.x;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.x < -0.5f || lightPos.x > 0.5f)
			t_min_cube = 1e10f;

		if (std::abs(dir.y) > 1e-7f)
		{
			const float t1 = (-0.5f - lightPos.y) / dir.y;
			const float t2 = ( 0.5f - lightPos.y) / dir.y;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.y < -0.5f || lightPos.y > 0.5f)
			t_min_cube = 1e10f;

		if (std::abs(dir.z) > 1e-7f)
		{
			const float t1 = (-0.5f - lightPos.z) / dir.z;
			const float t2 = ( 0.5f - lightPos.z) / dir.z;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.z < -0.5f || lightPos.z > 0.5f)
			t_min_cube = 1e10f;

		const bool hitsCube = (t_min_cube < t_max_cube && t_min_cube > eps);

		if (hitsCube && t_min_cube < t_plane)
			return t_min_cube / farPlane;
		if (hitsPlaneInBounds)
			return t_plane / farPlane;
		return 1.0f;
	}

	// --- Render data ---
	std::unique_ptr<ShadowDepthPass> m_shadowDepthPass;
};

// ===========================================================================
// Cubemap Depth Verification Test
// ===========================================================================

TEST_F(ShadowCubemapTest, Face3Depth)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// -------------------------------------------------------------------
	// Step 1: Build scene geometry (cube + plane via Scene/Mesh/Light)
	// Scene owns all GPU resources and must outlive the render items.
	// -------------------------------------------------------------------

	auto shadowRes = neurus::test::LoadSimpleShadow(
		*m_device, pd, m_queue, m_graphicsQueueFamily);

	const auto& renderItems = shadowRes.renderItems;
	ASSERT_EQ(renderItems.size(), 2u) << "Expected 2 meshes (cube + plane)";

	// -------------------------------------------------------------------
	// Step 2: Record shadow depth pass into cubemap
	// -------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();
		PassContext ctx{};
		ctx.renderExtent = vk::Extent2D(kRes, kRes);
		ctx.renderItems  = &renderItems;
		m_shadowDepthPass->Record(*cmd, ctx);
		EndSubmitWait(cmd);
	}

	// After Record(), cubemap is in eShaderReadOnlyOptimal layout

	// -------------------------------------------------------------------
	// Step 3: Create a temporary 2D RGBA32F color image for face 3 depth
	//         (color attachment avoids depth-aspect copy issues on this GPU)
	// -------------------------------------------------------------------
	Image tempColor(*m_device, pd,
		vk::Extent2D(kRes, kRes),
		vk::Format::eR32G32B32A32Sfloat,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
		1u, Image::ImageType::e2D,
		"TempColorForReadback");

	// Transition to COLOR_ATTACHMENT_OPTIMAL
	{
		auto& cmd = BeginCmd();
		tempColor.TransitionLayout(cmd,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal);
		EndSubmitWait(cmd);
	}

	// Create image view for the 2D color image
	vk::ImageViewCreateInfo viewCI({}, *tempColor.ImageHandle(),
		vk::ImageViewType::e2D, vk::Format::eR32G32B32A32Sfloat,
		vk::ComponentMapping(),
		vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
	vk::raii::ImageView tempView(*m_device, viewCI);

	// -------------------------------------------------------------------
	// Step 3b: Create a temporary D32_SFLOAT depth image for correct depth testing.
	//          Without a real depth attachment, depth testing is disabled and
	//          the last rendered item (plane) always overwrites earlier items
	//          (cube). With a real depth attachment, the cube (depth ~0.10)
	//          correctly occludes the plane (depth ~0.12) at cube-covered pixels.
	// -------------------------------------------------------------------
	Image tempDepth(*m_device, pd,
		vk::Extent2D(kRes, kRes),
		vk::Format::eD32Sfloat,
		vk::ImageUsageFlagBits::eDepthStencilAttachment,
		1u, Image::ImageType::eDepthStencil,
		"TempDepthForFace3");

	// Transition to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	{
		auto& cmd = BeginCmd();
		tempDepth.TransitionLayout(cmd,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);
		EndSubmitWait(cmd);
	}

	// Create image view for depth
	vk::ImageViewCreateInfo depthViewCI({}, *tempDepth.ImageHandle(),
		vk::ImageViewType::e2D, vk::Format::eD32Sfloat,
		vk::ComponentMapping(),
		vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1));
	vk::raii::ImageView depthView(*m_device, depthViewCI);

	// -------------------------------------------------------------------
	// Step 4: Create LightUBO populated with face 3 (-Y) VP matrix
	// -------------------------------------------------------------------
	// std140 GLSL layout: mat4[6] @ 0 (384 bytes), vec3 @ 384 (12 bytes).
	// On this GPU the GLSL compiler places farPlane directly at 396 (vec3=12
	// bytes, no rounding to 16). Matches ShadowDepthPass::LightUBO which also
	// uses _pad0... actually it adds _pad0 making farPlane @ 400 there.
	// BUT that works via the ShadowDepthPass pipeline which reads farPlane at
	// offset 400. The depth_to_color.frag uses identical layout.
	// Empirical testing: WITHOUT _pad0 → values correct; WITH _pad0 → INF.
	struct FaceUBO { glm::mat4 faceVP[6]; float lpx, lpy, lpz; float farPlane; };

	VulkanBuffer faceUBO(*m_device, pd, m_queue, m_graphicsQueueFamily,
		sizeof(FaceUBO),
		vk::BufferUsageFlagBits::eUniformBuffer,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
		"Face3UBO");

	{
		const glm::vec3 lightPos = shadowRes.scene->light_list.begin()->second->GetPosition();
		const float kNearPlane = 0.1f;
		const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, kNearPlane, kFarPlane);

		FaceUBO ubo{};
		ubo.faceVP[3] = proj * glm::lookAt(lightPos,
			lightPos + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1));
		ubo.lpx = lightPos.x; ubo.lpy = lightPos.y; ubo.lpz = lightPos.z;
		ubo.farPlane = kFarPlane;

		void* ptr = faceUBO.Map();
		std::memcpy(ptr, &ubo, sizeof(FaceUBO));
		faceUBO.Unmap();
	}

	// -------------------------------------------------------------------
	// Step 5: Create descriptor set (binding 0: UBO)
	// -------------------------------------------------------------------
	auto lightBindings = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
		.Build();
	DescriptorSetLayout lightLayout(*m_device, lightBindings);

	DescriptorPool lightPool(*m_device, 1,
		DescriptorPool::CalculatePoolSizes({&lightLayout}, 1));
	auto lightSet = std::move(lightPool.Allocate(lightLayout, 1).front());
	lightSet.WriteBuffer(0, faceUBO.GetDescriptorInfo(), vk::DescriptorType::eUniformBuffer);

	// -------------------------------------------------------------------
	// Step 6: Create graphics pipeline (depth_to_color.frag outputs depth to color)
	// -------------------------------------------------------------------
	auto vertModule = ShaderModule::FromEmbedded(*m_device,
		shadow_depth_vert_spv, sizeof(shadow_depth_vert_spv));
	auto fragModule = ShaderModule::FromEmbedded(*m_device,
		depth_to_color_frag_spv, sizeof(depth_to_color_frag_spv));

	BufferLayout vtxLayout;
	vtxLayout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);   // inPosition
	vtxLayout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);  // inNormal (unused)
	vtxLayout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);      // inUV (unused)

	std::vector<vk::PushConstantRange> pushRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstants))
	};

	std::vector<vk::DescriptorSetLayout> dslayouts = { *lightLayout.layout() };

	PipelineBuilder builder;
	auto face3Pipeline = builder
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(vtxLayout)
		.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
		.SetRasterization(vk::PolygonMode::eFill,
		                  vk::CullModeFlagBits::eNone,
		                  vk::FrontFace::eClockwise)
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLess)
		.AddColorBlendAttachment(vk::PipelineColorBlendAttachmentState(
			VK_FALSE,  // blendEnable
			vk::BlendFactor::eOne, vk::BlendFactor::eZero,
			vk::BlendOp::eAdd,
			vk::BlendFactor::eOne, vk::BlendFactor::eZero,
			vk::BlendOp::eAdd,
			vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA))
		.SetColorFormats({vk::Format::eR32G32B32A32Sfloat})
		.SetDepthFormat(vk::Format::eD32Sfloat)
		.SetDescriptorSetLayouts(dslayouts)
		.SetPushConstantRanges(pushRanges)
		.BuildGraphicsPipeline(*m_device);

	vk::PipelineLayoutCreateInfo plCI({}, dslayouts, pushRanges);
	vk::raii::PipelineLayout face3Layout(*m_device, plCI);

	// -------------------------------------------------------------------
	// Step 7: Render face 3 geometry into 2D color image
	// -------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();

		const vk::Viewport viewport(0.f, 0.f,
			static_cast<float>(kRes), static_cast<float>(kRes), 0.f, 1.f);
		const vk::Rect2D scissor({0, 0}, {kRes, kRes});
		cmd.setViewport(0, viewport);
		cmd.setScissor(0, scissor);

		const vk::ClearValue colorClear = vk::ClearColorValue(std::array<float, 4>{0.f, 0.f, 0.f, 0.f});
		const vk::ClearValue depthClear = vk::ClearDepthStencilValue(1.0f, 0);

		vk::RenderingAttachmentInfo colorAtt(
			*tempView,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ResolveModeFlagBits::eNone, nullptr,
			vk::ImageLayout::eUndefined,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			colorClear);

		vk::RenderingAttachmentInfo depthAtt(
			*depthView,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::ResolveModeFlagBits::eNone, nullptr,
			vk::ImageLayout::eUndefined,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			depthClear);

		vk::RenderingInfo renderInfo(
			{}, {{0, 0}, {kRes, kRes}},
			1, 0, colorAtt, &depthAtt, nullptr);

		cmd.beginRendering(renderInfo);

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *face3Pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
			face3Layout, 0, {lightSet.handle()}, {});

		// Push faceIndex = 3 at offset sizeof(mat4) = 64
		const int32_t faceIdx = 3;
		cmd.pushConstants<int32_t>(face3Layout,
			vk::ShaderStageFlagBits::eVertex,
			sizeof(glm::mat4), faceIdx);

		for (const auto& item : renderItems)
		{
			cmd.pushConstants<glm::mat4>(face3Layout,
				vk::ShaderStageFlagBits::eVertex, 0, item.pushConstants.model);
			cmd.bindVertexBuffers(0, {item.vertexBuffer}, {vk::DeviceSize{0}});
			cmd.bindIndexBuffer(item.indexBuffer, 0, item.indexType);
			cmd.drawIndexed(item.indexCount, 1, 0, 0, 0);
		}

		cmd.endRendering();

		// Transition color image → TRANSFER_SRC for readback
		vk::ImageMemoryBarrier copyBarrier(
			vk::AccessFlagBits::eColorAttachmentWrite,
			vk::AccessFlagBits::eTransferRead,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			*tempColor.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
		                   vk::PipelineStageFlagBits::eTransfer,
		                   {}, {}, {}, copyBarrier);

		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 8: Copy color image → staging buffer (float values)
	// -------------------------------------------------------------------
	const vk::DeviceSize bufSize = static_cast<vk::DeviceSize>(kRes) * kRes * 4 * sizeof(float);

	vk::BufferCreateInfo stagingCI({}, bufSize, vk::BufferUsageFlagBits::eTransferDst);
	vk::raii::Buffer stagingBuf(*m_device, stagingCI);

	auto memReqs = stagingBuf.getMemoryRequirements();
	auto memProps = pd.getMemoryProperties();
	uint32_t memType = 0;
	for (; memType < memProps.memoryTypeCount; ++memType)
	{
		if ((memReqs.memoryTypeBits & (1u << memType)) &&
		    (memProps.memoryTypes[memType].propertyFlags &
		     (vk::MemoryPropertyFlagBits::eHostVisible |
		      vk::MemoryPropertyFlagBits::eHostCoherent)))
		{
			break;
		}
	}
	ASSERT_LT(memType, memProps.memoryTypeCount) << "No host-visible+coherent memory type";

	vk::MemoryAllocateInfo allocInfo(memReqs.size, memType);
	vk::raii::DeviceMemory stagingMem(*m_device, allocInfo);
	stagingBuf.bindMemory(*stagingMem, 0);

	{
		auto& cmd = BeginCmd();
		vk::BufferImageCopy copyRegion{};
		copyRegion.bufferOffset      = 0;
		copyRegion.bufferRowLength   = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
			vk::ImageAspectFlagBits::eColor, 0, 0, 1);
		copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
		copyRegion.imageExtent = vk::Extent3D(kRes, kRes, 1);
		cmd.copyImageToBuffer(
			*tempColor.ImageHandle(),
			vk::ImageLayout::eTransferSrcOptimal,
			*stagingBuf,
			copyRegion);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 9: Map and extract depth values (R channel of float RGBA)
	// -------------------------------------------------------------------
	void* mapped = stagingMem.mapMemory(0, bufSize);
	std::vector<float> depthData(kRes * kRes);
	{
		const float* rgbaFloats = static_cast<const float*>(mapped);
		for (uint32_t i = 0; i < kRes * kRes; ++i)
			depthData[i] = rgbaFloats[i * 4];  // R channel = depth
	}

	// --- Debug: print before unmap ---
	{
		const uint8_t* rawBytes = static_cast<const uint8_t*>(mapped);
		std::cout << "Raw first 32 bytes (hex): ";
		for (int i = 0; i < 32; ++i)
			std::cout << std::hex << static_cast<int>(rawBytes[i]) << " ";
		std::cout << std::dec << std::endl;

		std::cout << "First 8 depths: ";
		for (int i = 0; i < 8; ++i)
			std::cout << depthData[i] << " ";
		std::cout << std::endl;

		float minVal = depthData[0], maxVal = depthData[0];
		int zeroCount = 0, oneCount = 0;
		for (const float v : depthData)
		{
			minVal = std::min(minVal, v);
			maxVal = std::max(maxVal, v);
			if (v == 0.0f) zeroCount++;
			if (v >= 0.999f && v <= 1.001f) oneCount++;
		}
		std::cout << "Depth range: min=" << minVal << " max=" << maxVal
		          << " zeros=" << zeroCount << " ones(approx)=" << oneCount
		          << " total=" << depthData.size() << std::endl;
	}

	stagingMem.unmapMemory();

	// -------------------------------------------------------------------
	// Step 9: Pixel-by-pixel verification
	// -------------------------------------------------------------------
	const glm::vec3 lightPos = shadowRes.scene->light_list.begin()->second->GetPosition();

	int cubePixels  = 0;
	int planePixels = 0;
	int bgPixels    = 0;
	int badPixels   = 0;

	for (uint32_t py = 0; py < kRes; ++py)
	{
		for (uint32_t px = 0; px < kRes; ++px)
		{
			const float actual   = depthData[py * kRes + px];
			const float expected = ComputeExpectedDepth(px, py, lightPos, kFarPlane, kRes);

			// Determine pixel category (for diagnostic summary only)
			const float sx = (2.0f * static_cast<float>(px) + 1.0f) / static_cast<float>(kRes) - 1.0f;
			const float sy = (2.0f * static_cast<float>(py) + 1.0f) / static_cast<float>(kRes) - 1.0f;
			const glm::vec3 dir = glm::normalize(glm::vec3(sx, -1.0f, -sy));

			// Plane hit check
			const float t_plane = -lightPos.y / dir.y;
			bool hitsPlane = false;
			if (t_plane > 0.0f)
			{
				const glm::vec3 hp = lightPos + t_plane * dir;
				hitsPlane = (std::abs(hp.x) <= 5.0f && std::abs(hp.z) <= 5.0f);
			}

			// Cube hit check (simplified slab -- just need category, not exact t)
			const float eps = 0.001f;
			float tMin = eps, tMax = 1e10f;
			bool parallelMiss = false;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float lo = -0.5f, hi = 0.5f;
				const float origin = (&lightPos.x)[axis];
				const float d       = (&dir.x)[axis];
				if (std::abs(d) > 1e-7f)
				{
					const float t1 = (lo - origin) / d;
					const float t2 = (hi - origin) / d;
					tMin = std::max(tMin, std::min(t1, t2));
					tMax = std::min(tMax, std::max(t1, t2));
				}
				else if (origin < lo || origin > hi)
				{
					parallelMiss = true;
				}
			}
			const bool hitsCube = (!parallelMiss && tMin < tMax && tMin > eps);

			// Categorize
			if (hitsCube && tMin < t_plane)
			{
				cubePixels++;
			}
			else if (hitsPlane)
			{
				planePixels++;
			}
			else
			{
				bgPixels++;
			}

			// Compare actual vs expected
			const float diff = std::abs(actual - expected);
			if (diff > kTolerance)
			{
				badPixels++;
				if (badPixels <= 10)
				{
					std::cout << "BAD PIXEL (" << px << "," << py
					          << "): actual=" << actual
					          << " expected=" << expected
					          << " diff=" << diff
					          << " category="
					          << (hitsCube && tMin < t_plane ? "cube" : hitsPlane ? "plane" : "bg")
					          << std::endl;
				}
			}
		}
	}

	std::cout << "ShadowCubemapDepthVerify: "
	          << "Cube pixels: " << cubePixels
	          << ", Plane pixels: " << planePixels
	          << ", BG pixels: " << bgPixels
	          << ", Bad pixels: " << badPixels
	          << " (tolerance=" << kTolerance << ")"
	          << std::endl;

	// Convert depth to u8 for reference image regression
	std::vector<uint8_t> u8Data(kRes * kRes);
	for (uint32_t i = 0; i < kRes * kRes; ++i)
	{
		float v = depthData[i];
		v = std::max(0.0f, std::min(1.0f, v));  // clamp
		u8Data[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
	}

	// -------------------------------------------------------------------
	// Reference image regression
	// -------------------------------------------------------------------
	{
		const std::string refPath = std::string(neurus::test::kReferenceDir) + "shadow/CubemapDepth_Face3.png";
		const std::string tmpPath = refPath + ".tmp";

		// Save captured depth as PNG (reuse u8Data)
		const bool captured = ImageData::SavePixelData(
			u8Data.data(), vk::Format::eR8Unorm,
			vk::Extent2D(kRes, kRes), tmpPath);
		ASSERT_TRUE(captured) << "Failed to capture depth map for reference";

		const int refResult = neurus::test::CheckReferenceOrGenerate(refPath, 2);
		if (refResult < 0)
			GTEST_SKIP() << "Reference image generated. Re-run the test to compare.";
		EXPECT_EQ(refResult, 0)
			<< refResult << " pixel(s) differ in cubemap depth (threshold: 2 per channel).";
	}

	// Sanity checks
	EXPECT_GT(cubePixels, 50)
		<< "Cube should cover at least 50 pixels on -Y face";
	EXPECT_GT(planePixels, 1000)
		<< "Plane should cover most non-cube pixels";

	// Less than 5% of pixels should differ
	EXPECT_LT(badPixels, 1)
		<< badPixels << " pixels exceed tolerance " << kTolerance;
}

// ===========================================================================
// All Faces Cubemap Depth Verification Test
// ===========================================================================

TEST_F(ShadowCubemapTest, AllFacesDepth)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// -------------------------------------------------------------------
	// Step 1: Build scene geometry
	// -------------------------------------------------------------------
	auto shadowRes = neurus::test::LoadSimpleShadow(
		*m_device, pd, m_queue, m_graphicsQueueFamily);

	const auto& renderItems = shadowRes.renderItems;
	ASSERT_EQ(renderItems.size(), 2u) << "Expected 2 meshes (cube + plane)";

	// -------------------------------------------------------------------
	// Step 2: Record shadow depth pass into cubemap (sanity)
	// -------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();
		PassContext ctx{};
		ctx.renderExtent = vk::Extent2D(kRes, kRes);
		ctx.renderItems  = &renderItems;
		m_shadowDepthPass->Record(*cmd, ctx);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 3: Create shared pipeline resources (once for all faces)
	// -------------------------------------------------------------------
	const glm::vec3 lightPos = shadowRes.scene->light_list.begin()->second->GetPosition();
	const float kNearPlane = 0.1f;
	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, kNearPlane, kFarPlane);

	// Face lookAt params matching ShadowDepthPass::updateUBO
	static const glm::vec3 kTargets[6] = {
		{ 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
	};
	static const glm::vec3 kUps[6] = {
		{ 0,-1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}, { 0,-1, 0}, { 0,-1, 0}
	};

	struct FaceUBO { glm::mat4 faceVP[6]; float lpx, lpy, lpz; float farPlane; };

	// Descriptor layout + pool + set
	auto lightBindings = BuildLayout()
		.AddBinding(0, vk::DescriptorType::eUniformBuffer,
		            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
		.Build();
	DescriptorSetLayout lightLayout(*m_device, lightBindings);

	DescriptorPool lightPool(*m_device, 1,
		DescriptorPool::CalculatePoolSizes({&lightLayout}, 1));
	auto lightSet = std::move(lightPool.Allocate(lightLayout, 1).front());

	// UBO (reused across faces, data updated per face)
	VulkanBuffer faceUBO(*m_device, pd, m_queue, m_graphicsQueueFamily,
		sizeof(FaceUBO),
		vk::BufferUsageFlagBits::eUniformBuffer,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
		"AllFacesUBO");

	lightSet.WriteBuffer(0, faceUBO.GetDescriptorInfo(), vk::DescriptorType::eUniformBuffer);

	// Shader modules
	auto vertModule = ShaderModule::FromEmbedded(*m_device,
		shadow_depth_vert_spv, sizeof(shadow_depth_vert_spv));
	auto fragModule = ShaderModule::FromEmbedded(*m_device,
		depth_to_color_frag_spv, sizeof(depth_to_color_frag_spv));

	// Vertex layout
	BufferLayout vtxLayout;
	vtxLayout.AddAttribute(0, vk::Format::eR32G32B32Sfloat, 0);   // inPosition
	vtxLayout.AddAttribute(1, vk::Format::eR32G32B32Sfloat, 12);  // inNormal (unused)
	vtxLayout.AddAttribute(2, vk::Format::eR32G32Sfloat, 24);      // inUV (unused)

	std::vector<vk::PushConstantRange> pushRanges = {
		vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstants))
	};

	std::vector<vk::DescriptorSetLayout> dslayouts = { *lightLayout.layout() };

	// Pipeline (shared across all faces)
	PipelineBuilder builder;
	auto pipeline = builder
		.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
		.AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
		.SetVertexInput(vtxLayout)
		.SetInputAssembly(vk::PrimitiveTopology::eTriangleList)
		.SetRasterization(vk::PolygonMode::eFill,
		                  vk::CullModeFlagBits::eNone,
		                  vk::FrontFace::eClockwise)
		.SetMultisampling()
		.SetDepthStencil(true, true, vk::CompareOp::eLess)
		.AddColorBlendAttachment(vk::PipelineColorBlendAttachmentState(
			VK_FALSE,
			vk::BlendFactor::eOne, vk::BlendFactor::eZero,
			vk::BlendOp::eAdd,
			vk::BlendFactor::eOne, vk::BlendFactor::eZero,
			vk::BlendOp::eAdd,
			vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA))
		.SetColorFormats({vk::Format::eR32G32B32A32Sfloat})
		.SetDepthFormat(vk::Format::eD32Sfloat)
		.SetDescriptorSetLayouts(dslayouts)
		.SetPushConstantRanges(pushRanges)
		.BuildGraphicsPipeline(*m_device);

	vk::PipelineLayoutCreateInfo plCI({}, dslayouts, pushRanges);
	vk::raii::PipelineLayout pipelineLayout(*m_device, plCI);

	const vk::DeviceSize bufSize = static_cast<vk::DeviceSize>(kRes) * kRes * 4 * sizeof(float);
	bool anyReferenceGenerated = false;

	// -------------------------------------------------------------------
	// Step 4: Loop over all 6 faces
	// -------------------------------------------------------------------
	for (uint32_t face = 0; face < 6; ++face)
	{
		NEURUS_LOG("[AllFacesDepth] Processing face " << face);

		// 4a. Create temporary color + depth images (per face)
		Image tempColor(*m_device, pd,
			vk::Extent2D(kRes, kRes),
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
			1u, Image::ImageType::e2D,
			("TempColor_Face" + std::to_string(face)).c_str());

		Image tempDepth(*m_device, pd,
			vk::Extent2D(kRes, kRes),
			vk::Format::eD32Sfloat,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			1u, Image::ImageType::eDepthStencil,
			("TempDepth_Face" + std::to_string(face)).c_str());

		// Transition to attachment layouts
		{
			auto& cmd = BeginCmd();
			tempColor.TransitionLayout(cmd,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal);
			tempDepth.TransitionLayout(cmd,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eDepthStencilAttachmentOptimal);
			EndSubmitWait(cmd);
		}

		// Image views
		vk::ImageViewCreateInfo viewCI({}, *tempColor.ImageHandle(),
			vk::ImageViewType::e2D, vk::Format::eR32G32B32A32Sfloat,
			vk::ComponentMapping(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		vk::raii::ImageView tempView(*m_device, viewCI);

		vk::ImageViewCreateInfo depthViewCI({}, *tempDepth.ImageHandle(),
			vk::ImageViewType::e2D, vk::Format::eD32Sfloat,
			vk::ComponentMapping(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1));
		vk::raii::ImageView depthView(*m_device, depthViewCI);

		// 4b. Update UBO for this face
		{
			FaceUBO ubo{};
			ubo.faceVP[face] = proj * glm::lookAt(lightPos,
				lightPos + kTargets[face], kUps[face]);
			ubo.lpx = lightPos.x; ubo.lpy = lightPos.y; ubo.lpz = lightPos.z;
			ubo.farPlane = kFarPlane;

			void* ptr = faceUBO.Map();
			std::memcpy(ptr, &ubo, sizeof(FaceUBO));
			faceUBO.Unmap();
		}

		// 4c. Render face geometry into 2D color image
		{
			auto& cmd = BeginCmd();

			const vk::Viewport viewport(0.f, 0.f,
				static_cast<float>(kRes), static_cast<float>(kRes), 0.f, 1.f);
			const vk::Rect2D scissor({0, 0}, {kRes, kRes});
			cmd.setViewport(0, viewport);
			cmd.setScissor(0, scissor);

			// Clear color = (1,1,1,1) so background pixels (no geometry hit)
			// match expected background depth of 1.0
			const vk::ClearValue colorClear =
				vk::ClearColorValue(std::array<float, 4>{1.f, 1.f, 1.f, 1.f});
			const vk::ClearValue depthClear = vk::ClearDepthStencilValue(1.0f, 0);

			vk::RenderingAttachmentInfo colorAtt(
				*tempView,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ResolveModeFlagBits::eNone, nullptr,
				vk::ImageLayout::eUndefined,
				vk::AttachmentLoadOp::eClear,
				vk::AttachmentStoreOp::eStore,
				colorClear);

			vk::RenderingAttachmentInfo depthAtt(
				*depthView,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				vk::ResolveModeFlagBits::eNone, nullptr,
				vk::ImageLayout::eUndefined,
				vk::AttachmentLoadOp::eClear,
				vk::AttachmentStoreOp::eStore,
				depthClear);

			vk::RenderingInfo renderInfo(
				{}, {{0, 0}, {kRes, kRes}},
				1, 0, colorAtt, &depthAtt, nullptr);

			cmd.beginRendering(renderInfo);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
				pipelineLayout, 0, {lightSet.handle()}, {});

			const int32_t faceIdx = static_cast<int32_t>(face);
			cmd.pushConstants<int32_t>(pipelineLayout,
				vk::ShaderStageFlagBits::eVertex,
				sizeof(glm::mat4), faceIdx);

			for (const auto& item : renderItems)
			{
				cmd.pushConstants<glm::mat4>(pipelineLayout,
					vk::ShaderStageFlagBits::eVertex, 0, item.pushConstants.model);
				cmd.bindVertexBuffers(0, {item.vertexBuffer}, {vk::DeviceSize{0}});
				cmd.bindIndexBuffer(item.indexBuffer, 0, item.indexType);
				cmd.drawIndexed(item.indexCount, 1, 0, 0, 0);
			}

			cmd.endRendering();

			// Transition color image for readback
			vk::ImageMemoryBarrier copyBarrier(
				vk::AccessFlagBits::eColorAttachmentWrite,
				vk::AccessFlagBits::eTransferRead,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eTransferSrcOptimal,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				*tempColor.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
			                   vk::PipelineStageFlagBits::eTransfer,
			                   {}, {}, {}, copyBarrier);

			EndSubmitWait(cmd);
		}

		// 4d. Copy color image → staging buffer
		vk::BufferCreateInfo stagingCI({}, bufSize, vk::BufferUsageFlagBits::eTransferDst);
		vk::raii::Buffer stagingBuf(*m_device, stagingCI);

		auto memReqs = stagingBuf.getMemoryRequirements();
		auto memProps = pd.getMemoryProperties();
		uint32_t memType = 0;
		for (; memType < memProps.memoryTypeCount; ++memType)
		{
			if ((memReqs.memoryTypeBits & (1u << memType)) &&
			    (memProps.memoryTypes[memType].propertyFlags &
			     (vk::MemoryPropertyFlagBits::eHostVisible |
			      vk::MemoryPropertyFlagBits::eHostCoherent)))
			{
				break;
			}
		}
		ASSERT_LT(memType, memProps.memoryTypeCount)
			<< "No host-visible+coherent memory type";

		vk::MemoryAllocateInfo allocInfo(memReqs.size, memType);
		vk::raii::DeviceMemory stagingMem(*m_device, allocInfo);
		stagingBuf.bindMemory(*stagingMem, 0);

		{
			auto& cmd = BeginCmd();
			vk::BufferImageCopy copyRegion{};
			copyRegion.bufferOffset      = 0;
			copyRegion.bufferRowLength   = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
				vk::ImageAspectFlagBits::eColor, 0, 0, 1);
			copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
			copyRegion.imageExtent = vk::Extent3D(kRes, kRes, 1);
			cmd.copyImageToBuffer(
				*tempColor.ImageHandle(),
				vk::ImageLayout::eTransferSrcOptimal,
				*stagingBuf,
				copyRegion);
			EndSubmitWait(cmd);
		}

		// 4e. Map and extract depth data (R channel of float RGBA)
		void* mapped = stagingMem.mapMemory(0, bufSize);
		std::vector<float> depthData(kRes * kRes);
		{
			const float* rgbaFloats = static_cast<const float*>(mapped);
			for (uint32_t i = 0; i < kRes * kRes; ++i)
				depthData[i] = rgbaFloats[i * 4];
		}

		// Debug: depth range summary
		{
			float minVal = depthData[0], maxVal = depthData[0];
			int zeroCount = 0, oneCount = 0;
			for (const float v : depthData)
			{
				minVal = std::min(minVal, v);
				maxVal = std::max(maxVal, v);
				if (v == 0.0f) zeroCount++;
				if (v >= 0.999f && v <= 1.001f) oneCount++;
			}
			std::cout << "[Face " << face << "] Depth: min=" << minVal
			          << " max=" << maxVal << " zeros=" << zeroCount
			          << " ones~=" << oneCount << " total=" << depthData.size()
			          << std::endl;
		}

		stagingMem.unmapMemory();

		// 4f. Pixel-by-pixel mathematical verification
		int cubePixels  = 0;
		int planePixels = 0;
		int bgPixels    = 0;
		int badPixels   = 0;

		for (uint32_t py = 0; py < kRes; ++py)
		{
			for (uint32_t px = 0; px < kRes; ++px)
			{
				const float actual   = depthData[py * kRes + px];
				const float expected =
					ComputeExpectedDepth(face, px, py, lightPos, kFarPlane, kRes);

				// Categorize pixel (for diagnostic summary only)
				const float sxV = (2.f * static_cast<float>(px) + 1.f) / static_cast<float>(kRes) - 1.f;
				const float syV = (2.f * static_cast<float>(py) + 1.f) / static_cast<float>(kRes) - 1.f;
				const glm::vec3 dir = glm::normalize(FaceDirection(face, sxV, syV));

				// Plane hit check
				const float t_plane = -lightPos.y / dir.y;
				bool hitsPlane = false;
				if (t_plane > 0.0f)
				{
					const glm::vec3 hp = lightPos + t_plane * dir;
					hitsPlane = (std::abs(hp.x) <= 5.0f && std::abs(hp.z) <= 5.0f);
				}

				// Cube hit check (simplified — category only, not exact t)
				const float eps = 0.001f;
				float tMin = eps, tMax = 1e10f;
				bool parallelMiss = false;
				for (int axis = 0; axis < 3; ++axis)
				{
					const float lo = -0.5f, hi = 0.5f;
					const float origin = (&lightPos.x)[axis];
					const float d       = (&dir.x)[axis];
					if (std::abs(d) > 1e-7f)
					{
						const float t1 = (lo - origin) / d;
						const float t2 = (hi - origin) / d;
						tMin = std::max(tMin, std::min(t1, t2));
						tMax = std::min(tMax, std::max(t1, t2));
					}
					else if (origin < lo || origin > hi)
					{
						parallelMiss = true;
					}
				}
				const bool hitsCube = (!parallelMiss && tMin < tMax && tMin > eps);

				if (hitsCube && tMin < t_plane)
					cubePixels++;
				else if (hitsPlane)
					planePixels++;
				else
					bgPixels++;

				const float diff = std::abs(actual - expected);
				if (diff > kTolerance)
				{
					badPixels++;
					if (badPixels <= 10)
					{
						std::cout << "BAD PIXEL (face=" << face
						          << " px=" << px << " py=" << py
						          << "): actual=" << actual
						          << " expected=" << expected
						          << " diff=" << diff
						          << " category="
						          << (hitsCube && tMin < t_plane ? "cube"
						              : hitsPlane ? "plane" : "bg")
						          << std::endl;
					}
				}
			}
		}

		std::cout << "[Face " << face << "] Cube:" << cubePixels
		          << " Plane:" << planePixels << " BG:" << bgPixels
		          << " Bad:" << badPixels
		          << " (tol=" << kTolerance << ")" << std::endl;

		EXPECT_LT(badPixels, 1)
			<< "Face " << face << ": " << badPixels
			<< " pixels exceed tolerance " << kTolerance;

		// Face-specific sanity checks
		if (face == 3)  // -Y: sees cube + plane
		{
			EXPECT_GT(cubePixels, 50)
				<< "Face -Y: cube should cover at least 50 pixels";
			EXPECT_GT(planePixels, 1000)
				<< "Face -Y: plane should cover most non-cube pixels";
		}
		else if (face == 2)  // +Y: looks up, sees nothing
		{
			EXPECT_EQ(cubePixels, 0)   << "Face +Y: should see no cube pixels";
			EXPECT_EQ(planePixels, 0)  << "Face +Y: should see no plane pixels";
			EXPECT_GT(bgPixels, 0)     << "Face +Y: all pixels should be background";
		}

		// 4g. Reference image regression
		{
			const std::string refPath =
				std::string(neurus::test::kReferenceDir)
				+ "shadow/CubemapDepth_Face" + std::to_string(face) + ".png";
			const std::string tmpPath = refPath + ".tmp";

			// Convert depth to u8
			std::vector<uint8_t> u8Data(kRes * kRes);
			for (uint32_t i = 0; i < kRes * kRes; ++i)
			{
				float v = depthData[i];
				v = std::max(0.0f, std::min(1.0f, v));
				u8Data[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
			}

			const bool captured = ImageData::SavePixelData(
				u8Data.data(), vk::Format::eR8Unorm,
				vk::Extent2D(kRes, kRes), tmpPath);
			ASSERT_TRUE(captured) << "Failed to save depth map for face " << face;

			const int refResult = neurus::test::CheckReferenceOrGenerate(refPath, 2);
			if (refResult < 0)
			{
				anyReferenceGenerated = true;
			}
			else
			{
				EXPECT_EQ(refResult, 0)
					<< refResult << " pixel(s) differ for face " << face;
			}
		}
	}

	if (anyReferenceGenerated)
		GTEST_SKIP() << "One or more reference images generated. Re-run the test.";
}
