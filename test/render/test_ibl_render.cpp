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

// --- STB image load ---
#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// Test vertex structure (matches BufferLayout: pos(3) + normal(3) + uv(2))
// ---------------------------------------------------------------------------
struct IBLTestVertex
{
	float posX, posY, posZ;
	float nrmX, nrmY, nrmZ;
	float uvX,  uvY;
};

// ---------------------------------------------------------------------------
// Reference image directory (relative to build/debug/test/)
// ---------------------------------------------------------------------------
static const char* kReferenceDir = "../../../test/render/reference/";

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
		m_iblPass->Generate(*m_equirectImage);

		// --- Wire IBL resources into lighting pass ---
		const auto& diffuseCubemap  = m_iblPass->GetDiffuseCubemap();
		const auto& specularCubemap = m_iblPass->GetSpecularCubemap();
		const auto& diffSampler     = m_iblPass->GetDiffuseSampler();
		const auto& specSampler     = m_iblPass->GetSpecularSampler();

		m_lightingPass->SetIBLResources(
			*diffuseCubemap.ImageViewHandle(),
			*diffSampler,
			*specularCubemap.ImageViewHandle(),
			*specSampler);
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	// --- Camera UBO ---
	static CameraUBOData ComputeCameraUBO(Camera& cam)
	{
		CameraUBOData ubo;
		ubo.view = cam.GetViewMatrix();
		ubo.viewProj = cam.GetProjectionMatrix() * ubo.view;
		return ubo;
	}

	// --- G-Buffer transition helper ---
	void TransitionGbufferToColorAttachment()
	{
		auto& cmd = BeginCmd();

		const std::array<AttachmentName, 4> colorAtts = {
			AttachmentName::Position,
			AttachmentName::Normal,
			AttachmentName::Albedo,
			AttachmentName::MetallicRoughness,
		};

		for (const auto& att : colorAtts)
		{
			m_attachmentManager->GetAttachment(att).TransitionLayout(
				cmd, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eColorAttachmentOptimal);
		}

		m_attachmentManager->GetAttachment(AttachmentName::Depth).TransitionLayout(
			cmd, vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);

		EndSubmitWait(cmd);
	}

	// --- Reference image comparison ---
	static bool LoadPng(const std::string& path,
	                    std::vector<uint8_t>& pixels,
	                    int& width, int& height, int& channels)
	{
		int w, h, c;
		unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
		if (!data) return false;

		width = w;
		height = h;
		channels = 4;
		const size_t byteCount = static_cast<size_t>(w) * h * 4;
		pixels.assign(data, data + byteCount);
		stbi_image_free(data);
		return true;
	}

	static int ComparePixels(const std::vector<uint8_t>& a,
	                         const std::vector<uint8_t>& b,
	                         int width, int height,
	                         int maxDiffPerChannel = 3)
	{
		const size_t count = static_cast<size_t>(width) * height * 4;
		if (a.size() != count || b.size() != count) return -1;

		int badPixels = 0;
		for (size_t i = 0; i < count; i += 4)
		{
			for (int c = 0; c < 4; ++c)
			{
				const int delta = std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c]));
				if (delta > maxDiffPerChannel)
				{
					++badPixels;
					break;
				}
			}
		}
		return badPixels;
	}

	static std::string ReferencePath()
	{
		return std::string(kReferenceDir) + "ibl/ibl_render.png";
	}

	// --- Render pass infrastructure ---
	std::unique_ptr<AttachmentManager>  m_attachmentManager;
	std::unique_ptr<RenderPassManager>  m_renderPassManager;
	std::unique_ptr<GeometryPass>       m_geometryPass;
	std::unique_ptr<LightingPass>       m_lightingPass;
	std::unique_ptr<IBLPass>            m_iblPass;
	std::unique_ptr<Image>              m_equirectImage;
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

	const auto& rawMesh = meshData->GetMeshData();
	const size_t srcVertexCount = rawMesh.dataArray.size() / 14;
	const size_t indexCount = rawMesh.indexArray.size();
	ASSERT_GT(srcVertexCount, 0u);
	ASSERT_GT(indexCount, 0u);

	std::vector<float>    srcVertexData = rawMesh.dataArray;
	std::vector<uint32_t> srcIndexData  = rawMesh.indexArray;

	// -------------------------------------------------------------------
	// Step 2: Create camera (pos (0, 2, 5), looking at origin)
	// -------------------------------------------------------------------
	auto camera = std::make_shared<Camera>(
		static_cast<float>(kRenderWidth),
		static_cast<float>(kRenderHeight),
		60.0f, 0.1f, 100.0f);
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	const CameraUBOData camUBO = ComputeCameraUBO(*camera);

	// -------------------------------------------------------------------
	// Step 3: Create point light (for mixed direct + IBL lighting)
	// -------------------------------------------------------------------
	auto light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(2.0f, 2.0f, 2.0f));
	light->light_radius = 10.0f;

	// -------------------------------------------------------------------
	// Step 4: Build mesh + material
	// -------------------------------------------------------------------
	auto material = std::make_shared<Material>();
	material->SetMatParam(Material::MAT_METAL, 0.0f);
	material->SetMatParam(Material::MAT_ROUGH, 0.5f);
	material->SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f, 1.0f, 1.0f));

	auto mesh = std::make_shared<Mesh>();
	mesh->o_mesh = meshData;
	mesh->o_material = material;

	// -------------------------------------------------------------------
	// Step 5: Convert vertex data (14 floats → 8: pos+normal+uv)
	// -------------------------------------------------------------------
	std::vector<IBLTestVertex> vertices(srcVertexCount);
	for (size_t i = 0; i < srcVertexCount; ++i)
	{
		const float* s = &srcVertexData[i * 14];
		IBLTestVertex& v = vertices[i];
		v.posX = s[0] * 0.25f; v.posY = s[1] * 0.25f; v.posZ = s[2] * 0.25f;
		v.nrmX = s[3]; v.nrmY = s[4]; v.nrmZ = s[5];
		v.uvX  = s[6]; v.uvY  = s[7];
	}

	std::vector<uint32_t> indices = srcIndexData;

	// -------------------------------------------------------------------
	// Step 6: Upload vertex + index buffers to GPU
	// -------------------------------------------------------------------
	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 vertices.data(), vertices.size() * sizeof(IBLTestVertex),
	                 sizeof(IBLTestVertex), static_cast<uint32_t>(vertices.size()));

	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                indices.data(), indices.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(indices.size()));

	GeometryRenderItem renderItem;
	renderItem.vertexBuffer = vbo.buffer();
	renderItem.indexBuffer  = ibo.buffer();
	renderItem.indexCount   = ibo.GetIndexCount();
	renderItem.indexType    = ibo.GetIndexType();
	renderItem.pushConstants.model = glm::mat4(1.0f);
	renderItem.pushConstants.normalMatrix = glm::mat4(1.0f);

	// -------------------------------------------------------------------
	// Step 7: Transition G-Buffer & record geometry pass
	// -------------------------------------------------------------------
	TransitionGbufferToColorAttachment();

	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, camUBO,
		                       { renderItem },
		                       { kRenderWidth, kRenderHeight });
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
		m_lightingPass->Record(*cmd,
		                       camera->GetPosition(),
		                       camUBO.view,
		                       glm::inverse(camUBO.viewProj),
		                       {kRenderWidth, kRenderHeight},
		                       0);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 9: Capture HDRColor & compare with reference
	// -------------------------------------------------------------------
	const std::string refPath = ReferencePath();
	const bool refExists = std::ifstream(refPath).good();

	// Create reference sub-directory if needed
	std::filesystem::create_directories(std::string(kReferenceDir) + "ibl/");

	const std::string tmpPath = refPath + ".tmp";

	Image& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);
	const bool captured = Screenshot::CaptureAttachment(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		hdrColor, tmpPath, false);

	ASSERT_TRUE(captured) << "Failed to capture HDRColor attachment";

	if (!refExists)
	{
		// First run — rename .tmp → .png to generate reference
		std::rename(tmpPath.c_str(), refPath.c_str());
		GTEST_SKIP() << "Reference image generated. Re-run the test to compare.";
	}
	else
	{
		// Compare against reference
		std::vector<uint8_t> tmpPixels, refPixels;
		int tmpW, tmpH, tmpC, refW, refH, refC;

		const bool tmpLoaded = LoadPng(tmpPath, tmpPixels, tmpW, tmpH, tmpC);
		const bool refLoaded = LoadPng(refPath, refPixels, refW, refH, refC);

		ASSERT_TRUE(tmpLoaded) << "Failed to load captured PNG: " << tmpPath;
		ASSERT_TRUE(refLoaded) << "Failed to load reference PNG: " << refPath;
		ASSERT_EQ(tmpW, refW) << "Width mismatch";
		ASSERT_EQ(tmpH, refH) << "Height mismatch";

		const int badPixels = ComparePixels(tmpPixels, refPixels, tmpW, tmpH, 3);

		// Clean up temp file
		std::remove(tmpPath.c_str());

		EXPECT_EQ(badPixels, 0)
			<< badPixels << " pixel(s) differ in IBL render (threshold: 3 per channel).";
	}
}
