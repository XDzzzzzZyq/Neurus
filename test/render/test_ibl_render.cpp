/**
 * @file test_ibl_render.cpp
 * @brief Reference-image regression test for IBL rendering.
 *
 * Generates a procedural colourful gradient HDR equirectangular image,
 * creates IBL cubemaps via IBLPass, renders an icosphere through the
 * deferred pipeline (geometry + PBR lighting with IBL), captures the
 * HDRColor output, and compares pixel‑wise against a reference PNG.
 *
 * On first run the reference image does not exist — the test generates it
 * automatically and reports SKIPPED.  Subsequent runs compare and FAIL on
 * any pixel difference exceeding the allowed tolerance.
 *
 * @note Requires a Vulkan 1.4‑capable GPU.  Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

// --- Render layer ---
#include "render/passes/AttachmentManager.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/IBLPass.h"
#include "render/passes/LightingPass.h"
#include "render/passes/PassContext.h"
#include "render/passes/RenderPassManager.h"
#include "render/Image.h"
#include "render/Material.h"
#include "render/Screenshot.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

// --- Asset layer ---
#include "asset/MeshData.h"

// --- Scene layer ---
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

// --- Embedded shaders ---
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <pbr_lighting.comp.h>
#include <irradiance_conv.comp.h>
#include <importance_samp.comp.h>

#include "shared/TestReferenceImage.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Procedural colourful gradient equirectangular image
//
// Horizontal: red (left) → blue (right)
// Vertical:   green mid-band, white equator
// Poles fade to dark
// Alpha = 1.0 everywhere
// ---------------------------------------------------------------------------
static std::vector<float> GenerateColorfulGradient(uint32_t width, uint32_t height)
{
	std::vector<float> pixels(static_cast<size_t>(width) * height * 4, 0.0f);

	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
			const float u = static_cast<float>(x) / static_cast<float>(width);
			const float v = static_cast<float>(y) / static_cast<float>(height);

			// Horizontal: red → blue
			float r = 1.0f - u;
			float g = 0.0f;
			float b = u;

			// Green vertical mid-band
			const float distFromEquator = std::abs(v - 0.5f) * 2.0f; // 0 at equator, 1 at poles
			const float greenStrength = std::max(0.0f, 1.0f - distFromEquator * 1.2f);
			g += greenStrength;

			// White equator blend
			const float equatorWeight = std::max(0.0f, 1.0f - distFromEquator * 1.5f);
			const float white = equatorWeight * 1.0f;
			r = r * (1.0f - equatorWeight) + white;
			g = g * (1.0f - equatorWeight) + white;
			b = b * (1.0f - equatorWeight) + white;

			// Fade to dark at poles
			const float poleFalloff = 1.0f - distFromEquator * 0.8f;
			r *= poleFalloff;
			g *= poleFalloff;
			b *= poleFalloff;

			pixels[idx + 0] = r;
			pixels[idx + 1] = g;
			pixels[idx + 2] = b;
			pixels[idx + 3] = 1.0f; // alpha
		}
	}

	return pixels;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class IBLRenderTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;
	static constexpr uint32_t kEquiWidth    = 512;
	static constexpr uint32_t kEquiHeight   = 256;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& dev = *m_device;
		auto& pd  = PhysicalDevice();

		// --- Render pass infrastructure ---
		m_attachmentManager = std::make_unique<AttachmentManager>(dev, pd);
		m_attachmentManager->Create({kRenderWidth, kRenderHeight});
		m_renderPassManager = std::make_unique<RenderPassManager>();

		// --- Geometry pass ---
		m_geometryPass = std::make_unique<GeometryPass>(
			dev, pd, m_queue, m_graphicsQueueFamily,
			*m_attachmentManager,
			*m_renderPassManager,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		// --- Lighting pass ---
		m_lightingPass = std::make_unique<LightingPass>(
			dev, pd,
			*m_attachmentManager,
			1u,  // single frame
			m_queue, m_graphicsQueueFamily,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));

		// --- IBL pass ---
		m_iblPass = std::make_unique<IBLPass>(
			dev, pd,
			m_queue, m_graphicsQueueFamily,
			irradiance_conv_comp_spv, sizeof(irradiance_conv_comp_spv),
			importance_samp_comp_spv, sizeof(importance_samp_comp_spv));

		// --- Create IBL cubemap Images (owned by test fixture) ---
		{
			const vk::ImageUsageFlags cubeUsage =
				vk::ImageUsageFlagBits::eStorage      // compute write
				| vk::ImageUsageFlagBits::eSampled    // shader read
				| vk::ImageUsageFlagBits::eTransferSrc; // readback

			m_diffuseCubemap = std::make_unique<Image>(
				dev, pd,
				vk::Extent2D{IBLPass::kDiffuseFaceRes, IBLPass::kDiffuseFaceRes},
				vk::Format::eR32G32B32A32Sfloat,
				cubeUsage,
				/*mipLevels=*/1,
				Image::ImageType::eCube,
				"IBLRenderTest_DiffuseCube");

			m_specularCubemap = std::make_unique<Image>(
				dev, pd,
				vk::Extent2D{IBLPass::kSpecularFaceRes, IBLPass::kSpecularFaceRes},
				vk::Format::eR32G32B32A32Sfloat,
				cubeUsage,
				/*mipLevels=*/IBLPass::kSpecularMipLevels,
				Image::ImageType::eCube,
				"IBLRenderTest_SpecularCube");

			// Create diffuse sampler (1 mip)
			vk::SamplerCreateInfo diffuseSamplerCI(
				{},
				vk::Filter::eLinear,
				vk::Filter::eLinear,
				vk::SamplerMipmapMode::eLinear,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				0.0f,
				VK_FALSE,
				0.0f,
				VK_FALSE,
				vk::CompareOp::eAlways,
				0.0f,
				1.0f,
				vk::BorderColor::eFloatTransparentBlack,
				VK_FALSE);
			m_diffuseSampler = vk::raii::Sampler(dev, diffuseSamplerCI);

			// Create specular sampler (kSpecularMipLevels mips)
			vk::SamplerCreateInfo specularSamplerCI(
				{},
				vk::Filter::eLinear,
				vk::Filter::eLinear,
				vk::SamplerMipmapMode::eLinear,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				vk::SamplerAddressMode::eClampToEdge,
				0.0f,
				VK_FALSE,
				0.0f,
				VK_FALSE,
				vk::CompareOp::eAlways,
				0.0f,
				static_cast<float>(IBLPass::kSpecularMipLevels),
				vk::BorderColor::eFloatTransparentBlack,
				VK_FALSE);
			m_specularSampler = vk::raii::Sampler(dev, specularSamplerCI);
		}

		// --- Generate equirect gradient & upload to GPU ---
		auto gradientPixels = GenerateColorfulGradient(kEquiWidth, kEquiHeight);

		m_equirectImage = std::make_unique<Image>(
			dev, pd,
			vk::Extent2D{kEquiWidth, kEquiHeight},
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eSampled |
			    vk::ImageUsageFlagBits::eTransferDst,
			/*mipLevels=*/1,
			Image::ImageType::e2D,
			"IBLRenderTest_Equirect");

		m_equirectImage->UploadPixelData(dev, pd, m_queue, m_graphicsQueueFamily,
		                                  gradientPixels.data(),
		                                  gradientPixels.size() * sizeof(float));

		// --- Generate IBL cubemaps ---
		m_iblPass->Generate(*m_equirectImage, *m_diffuseCubemap, *m_specularCubemap);

		// --- Wire IBL resources into lighting pass ---
		m_lightingPass->SetIBLResources(
			*m_diffuseCubemap->ImageViewHandle(),
			*m_diffuseSampler,
			*m_specularCubemap->ImageViewHandle(),
			*m_specularSampler);
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	// --- Render pass infrastructure ---
	std::unique_ptr<AttachmentManager>  m_attachmentManager;
	std::unique_ptr<RenderPassManager>  m_renderPassManager;
	std::unique_ptr<GeometryPass>       m_geometryPass;
	std::unique_ptr<LightingPass>       m_lightingPass;
	std::unique_ptr<IBLPass>            m_iblPass;
	std::unique_ptr<Image>              m_equirectImage;
	// --- IBL cubemaps (owned by test fixture, passed to IBLPass::Generate) ---
	std::unique_ptr<Image>              m_diffuseCubemap;
	std::unique_ptr<Image>              m_specularCubemap;
	// --- Cubemap samplers ---
	vk::raii::Sampler                   m_diffuseSampler = nullptr;
	vk::raii::Sampler                   m_specularSampler = nullptr;
};

// ===========================================================================
// IBL Render Reference Image Regression Test
// ===========================================================================

TEST_F(IBLRenderTest, IBLRender_MatchesReferenceImage)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// -------------------------------------------------------------------
	// Step 1: Load sphere OBJ
	// -------------------------------------------------------------------
	std::string objPath = ResolveAssetPath("res/obj/sphere.obj");

	auto meshData = std::make_shared<MeshData>();
	const bool loaded = meshData->LoadObj(objPath);
	ASSERT_TRUE(loaded) << "Failed to load OBJ: " << objPath;

	// Scale positions 0.25x (matches original manual vertex extraction scaling)
	{
		auto& raw = const_cast<MeshData::ByteArray&>(meshData->GetMeshData());
		const size_t vertexCount = raw.dataArray.size() / 14;
		ASSERT_GT(vertexCount, 0u);
		ASSERT_GT(raw.indexArray.size(), 0u);
		for (size_t i = 0; i < vertexCount; ++i)
		{
			raw.dataArray[i * 14 + 0] *= 0.25f;
			raw.dataArray[i * 14 + 1] *= 0.25f;
			raw.dataArray[i * 14 + 2] *= 0.25f;
		}
	}

	// -------------------------------------------------------------------
	// Step 2: Create camera (pos (0, 2, 5), looking at origin)
	// -------------------------------------------------------------------
	auto camera = std::make_shared<Camera>(
		static_cast<float>(kRenderWidth),
		static_cast<float>(kRenderHeight),
		60.0f, 0.1f, 100.0f);
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	const CameraUBOData camUBO = VulkanTestShared::ComputeCameraUBO(*camera);

	// -------------------------------------------------------------------
	// Step 3: Create point light (for mixed direct + IBL lighting)
	// -------------------------------------------------------------------
	auto light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(2.0f, 2.0f, 2.0f));
	light->light_radius = 10.0f;

	// -------------------------------------------------------------------
	// Step 4: Build mesh + material + upload to GPU
	// -------------------------------------------------------------------
	auto material = std::make_shared<Material>();
	material->SetMatParam(Material::MAT_METAL, 0.0f);
	material->SetMatParam(Material::MAT_ROUGH, 0.5f);
	material->SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f, 1.0f, 1.0f));

	auto mesh = std::make_shared<Mesh>();
	mesh->o_mesh = meshData;
	mesh->o_material = material;
	mesh->UploadToGPU(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);

	GeometryRenderItem renderItem;
	renderItem.vertexBuffer = mesh->GetVertexBuffer()->buffer();
	renderItem.indexBuffer  = mesh->GetIndexBuffer()->buffer();
	renderItem.indexCount   = mesh->GetGPUIndexCount();
	renderItem.indexType    = vk::IndexType::eUint32;
	renderItem.pushConstants.model = glm::mat4(1.0f);
	renderItem.pushConstants.normalMatrix = glm::mat4(1.0f);

	// -------------------------------------------------------------------
	// Step 7: Transition G-Buffer & record geometry pass
	// -------------------------------------------------------------------
	VulkanTestShared::TransitionGbufferToColorAttachment(*m_attachmentManager, *this);

	{
		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items = { renderItem };
		m_geometryPass->Record(*cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camUBO.viewProj,
			.view = camUBO.view,
			.renderItems = &items,
		});
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 8: Upload light SSBO & record lighting pass (with IBL)
	// -------------------------------------------------------------------
	{
		Scene scene;
		scene.UseLight(light);
		m_lightingPass->UploadLights(scene);
	}

	{
		auto& cmd = BeginCmd();
		m_lightingPass->Record(*cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.frameIndex = 0,
			.view = camUBO.view,
			.cameraPos = camera->GetPosition(),
			.invProjView = glm::inverse(camUBO.viewProj),
		});
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 9: Capture HDRColor & compare with reference
	// -------------------------------------------------------------------
	const std::string refPath = std::string(neurus::test::kReferenceDir) + "ibl/ibl_render.png";
	const std::string tmpPath = refPath + ".tmp";

	Image& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
	const bool captured = Screenshot::CaptureAttachment(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		hdrColor, tmpPath, false);

	ASSERT_TRUE(captured) << "Failed to capture HDRColor attachment";

	const int result = neurus::test::CheckReferenceOrGenerate(refPath, 3);
	if (result < 0)
		GTEST_SKIP() << "Reference image generated. Re-run the test to compare.";
	EXPECT_EQ(result, 0) << result << " pixel(s) differ in IBL render (threshold: 3 per channel).";
}

// ===========================================================================
// 5. Reload test — validates GPU resource lifetime across destroy/recreate
// ===========================================================================

TEST_F(IBLRenderTest, Reload_Environment_NoValidationErrors)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& dev = *m_device;
	auto& pd  = PhysicalDevice();

	// ================================================================
	// Phase 1 — Set up scene + IBL and render Frame 1
	// ================================================================

	// --- Load sphere OBJ ---
	std::string objPath = ResolveAssetPath("res/obj/sphere.obj");
	auto meshData = std::make_shared<MeshData>();
	ASSERT_TRUE(meshData->LoadObj(objPath)) << "Failed to load OBJ: " << objPath;

	// Scale positions 0.25x (matches original manual vertex extraction scaling)
	{
		auto& raw = const_cast<MeshData::ByteArray&>(meshData->GetMeshData());
		const size_t vertexCount = raw.dataArray.size() / 14;
		ASSERT_GT(vertexCount, 0u);
		ASSERT_GT(raw.indexArray.size(), 0u);
		for (size_t i = 0; i < vertexCount; ++i)
		{
			raw.dataArray[i * 14 + 0] *= 0.25f;
			raw.dataArray[i * 14 + 1] *= 0.25f;
			raw.dataArray[i * 14 + 2] *= 0.25f;
		}
	}

	// --- Camera ---
	auto camera = std::make_shared<Camera>(
		static_cast<float>(kRenderWidth),
		static_cast<float>(kRenderHeight),
		60.0f, 0.1f, 100.0f);
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const CameraUBOData camUBO = VulkanTestShared::ComputeCameraUBO(*camera);

	// --- Light ---
	auto light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(2.0f, 2.0f, 2.0f));
	light->light_radius = 10.0f;

	// --- Material ---
	auto material = std::make_shared<Material>();
	material->SetMatParam(Material::MAT_METAL, 0.0f);
	material->SetMatParam(Material::MAT_ROUGH, 0.5f);
	material->SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f, 1.0f, 1.0f));

	// --- Mesh + UploadToGPU ---
	auto mesh = std::make_shared<Mesh>();
	mesh->o_mesh = meshData;
	mesh->o_material = material;
	mesh->UploadToGPU(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily);

	GeometryRenderItem renderItem;
	renderItem.vertexBuffer = mesh->GetVertexBuffer()->buffer();
	renderItem.indexBuffer  = mesh->GetIndexBuffer()->buffer();
	renderItem.indexCount   = mesh->GetGPUIndexCount();
	renderItem.indexType    = vk::IndexType::eUint32;
	renderItem.pushConstants.model = glm::mat4(1.0f);
	renderItem.pushConstants.normalMatrix = glm::mat4(1.0f);

	// --- Render Frame 1 (IBL active) ---
	{
		VulkanTestShared::TransitionGbufferToColorAttachment(*m_attachmentManager, *this);

		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items = { renderItem };
		m_geometryPass->Record(*cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camUBO.viewProj,
			.view = camUBO.view,
			.renderItems = &items,
		});
		EndSubmitWait(cmd);
	}
	{
		Scene scene;
		scene.UseLight(light);
		m_lightingPass->UploadLights(scene);
	}
	{
		auto& cmd = BeginCmd();
		m_lightingPass->Record(*cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.frameIndex = 0,
			.view = camUBO.view,
			.cameraPos = camera->GetPosition(),
			.invProjView = glm::inverse(camUBO.viewProj),
		});
		EndSubmitWait(cmd);
	}

	// ================================================================
	// Phase 2 — Simulate project reload: destroy then recreate
	// ================================================================

	// 2a. Wait for all GPU work to finish
	m_device->waitIdle();

	// 2b. Reset IBL references in LightingPass so descriptor sets no
	//     longer reference the soon-to-be-destroyed cubemap ImageViews.
	SCOPED_TRACE("ResetIBLResources");
	m_lightingPass->ResetIBLResources();

	// 2c. Destroy render pass objects (reverse order of creation).
	//     Unique_ptr destructors handle Vulkan resource teardown.
	//     This simulates destroying DeferredRenderer on project close.
	SCOPED_TRACE("Destroy passes");
	m_iblPass.reset();
	m_lightingPass.reset();
	m_geometryPass.reset();
	m_renderPassManager.reset();
	m_attachmentManager.reset();

	// 2d. Destroy IBL GPU resources (cubemap images + samplers + equirect).
	SCOPED_TRACE("Destroy IBL resources");
	m_equirectImage.reset();
	m_diffuseCubemap.reset();
	m_specularCubemap.reset();
	m_diffuseSampler  = vk::raii::Sampler(nullptr);
	m_specularSampler = vk::raii::Sampler(nullptr);

	// 2e. Recreate AttachmentManager + passes (simulating renderer init).
	SCOPED_TRACE("Recreate passes");
	m_attachmentManager = std::make_unique<AttachmentManager>(dev, pd);
	m_attachmentManager->Create({kRenderWidth, kRenderHeight});
	m_renderPassManager = std::make_unique<RenderPassManager>();

	m_geometryPass = std::make_unique<GeometryPass>(
		dev, pd, m_queue, m_graphicsQueueFamily,
		*m_attachmentManager, *m_renderPassManager,
		gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
		gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

	m_lightingPass = std::make_unique<LightingPass>(
		dev, pd, *m_attachmentManager, 1u,
		m_queue, m_graphicsQueueFamily,
		pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));

	m_iblPass = std::make_unique<IBLPass>(
		dev, pd, m_queue, m_graphicsQueueFamily,
		irradiance_conv_comp_spv, sizeof(irradiance_conv_comp_spv),
		importance_samp_comp_spv, sizeof(importance_samp_comp_spv));

	// 2f. Re-create IBL cubemap Images + samplers.
	SCOPED_TRACE("Recreate IBL resources");
	{
		const vk::ImageUsageFlags cubeUsage =
			vk::ImageUsageFlagBits::eStorage
			| vk::ImageUsageFlagBits::eSampled
			| vk::ImageUsageFlagBits::eTransferSrc;

		m_diffuseCubemap = std::make_unique<Image>(
			dev, pd,
			vk::Extent2D{IBLPass::kDiffuseFaceRes, IBLPass::kDiffuseFaceRes},
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage,
			/*mipLevels=*/1,
			Image::ImageType::eCube,
			"IBLRenderTest_DiffuseCube_Reload");

		m_specularCubemap = std::make_unique<Image>(
			dev, pd,
			vk::Extent2D{IBLPass::kSpecularFaceRes, IBLPass::kSpecularFaceRes},
			vk::Format::eR32G32B32A32Sfloat,
			cubeUsage,
			IBLPass::kSpecularMipLevels,
			Image::ImageType::eCube,
			"IBLRenderTest_SpecularCube_Reload");
	}

	// Samplers (reuse same creation params as SetUp)
	{
		vk::SamplerCreateInfo diffuseSamplerCI(
			{}, vk::Filter::eLinear, vk::Filter::eLinear,
			vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToEdge,
			vk::SamplerAddressMode::eClampToEdge,
			vk::SamplerAddressMode::eClampToEdge,
			0.0f, VK_FALSE, 0.0f, VK_FALSE,
			vk::CompareOp::eAlways, 0.0f, 1.0f,
			vk::BorderColor::eFloatTransparentBlack, VK_FALSE);
		m_diffuseSampler = vk::raii::Sampler(dev, diffuseSamplerCI);

		vk::SamplerCreateInfo specularSamplerCI(
			{}, vk::Filter::eLinear, vk::Filter::eLinear,
			vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToEdge,
			vk::SamplerAddressMode::eClampToEdge,
			vk::SamplerAddressMode::eClampToEdge,
			0.0f, VK_FALSE, 0.0f, VK_FALSE,
			vk::CompareOp::eAlways, 0.0f,
			static_cast<float>(IBLPass::kSpecularMipLevels),
			vk::BorderColor::eFloatTransparentBlack, VK_FALSE);
		m_specularSampler = vk::raii::Sampler(dev, specularSamplerCI);
	}

	// 2g. Re-create equirect gradient + upload to GPU.
	{
		auto gradientPixels = GenerateColorfulGradient(kEquiWidth, kEquiHeight);
		m_equirectImage = std::make_unique<Image>(
			dev, pd,
			vk::Extent2D{kEquiWidth, kEquiHeight},
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eSampled |
			    vk::ImageUsageFlagBits::eTransferDst,
			/*mipLevels=*/1,
			Image::ImageType::e2D,
			"IBLRenderTest_Equirect_Reload");
		m_equirectImage->UploadPixelData(dev, pd, m_queue, m_graphicsQueueFamily,
		                                 gradientPixels.data(),
		                                 gradientPixels.size() * sizeof(float));
	}

	// 2h. Generate IBL cubemaps and wire into lighting pass.
	SCOPED_TRACE("Generate IBL cubemaps");
	m_iblPass->Generate(*m_equirectImage, *m_diffuseCubemap, *m_specularCubemap);

	SCOPED_TRACE("Wire IBL into LightingPass");
	m_lightingPass->SetIBLResources(
		*m_diffuseCubemap->ImageViewHandle(),
		*m_diffuseSampler,
		*m_specularCubemap->ImageViewHandle(),
		*m_specularSampler);

	// ================================================================
	// Phase 3 — Render Frame 2 (after reload, IBL active again)
	// ================================================================

	SCOPED_TRACE("Render Frame 2");
	{
		VulkanTestShared::TransitionGbufferToColorAttachment(*m_attachmentManager, *this);

		auto& cmd = BeginCmd();
		std::vector<GeometryRenderItem> items = { renderItem };
		m_geometryPass->Record(*cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.viewProj = camUBO.viewProj,
			.view = camUBO.view,
			.renderItems = &items,
		});
		EndSubmitWait(cmd);
	}
	{
		Scene scene;
		scene.UseLight(light);
		m_lightingPass->UploadLights(scene);
	}
	{
		auto& cmd = BeginCmd();
		m_lightingPass->Record(*cmd, PassContext{
			.renderExtent = {kRenderWidth, kRenderHeight},
			.frameIndex = 0,
			.view = camUBO.view,
			.cameraPos = camera->GetPosition(),
			.invProjView = glm::inverse(camUBO.viewProj),
		});
		EndSubmitWait(cmd);
	}

	// If we reached here without crashing or triggering Vulkan validation
	// errors, the GPU resource lifetime fix is verified.
	SUCCEED() << "Reload cycle completed without validation errors or crashes.";
}
