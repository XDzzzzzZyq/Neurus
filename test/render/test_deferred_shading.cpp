/**
 * @file test_deferred_shading.cpp
 * @brief Golden-image regression test for the deferred shading pipeline (G-Buffer + HDR passes).
 *
 * Renders a known scene (sphere OBJ + point light) at 256×256 through the
 * full deferred pipeline and captures Position, Normal, Albedo, MetallicRoughness
 * and HDRColor attachments as PNGs.  Each PNG is compared pixel‑wise against
 * a ground‑truth ("golden") image stored in test/render/golden/.
 *
 * On first run the golden images DO NOT exist — the test generates them
 * automatically and reports SKIPPED.  Subsequent runs compare and FAIL on
 * any pixel difference exceeding the allowed tolerance.
 *
 * @note Requires a Vulkan 1.4‑capable GPU.  Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanFixture.h"

// --- Render layer ---
#include "render/AttachmentManager.h"
#include "render/GeometryPass.h"
#include "render/LightingPass.h"
#include "render/Material.h"
#include "render/RenderPassManager.h"
#include "render/Screenshot.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"
#include "render/Texture.h"

// --- Data layer ---
#include "data/MeshData.h"

// --- Scene layer ---
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"

// --- Embedded shaders ---
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <pbr_lighting.comp.h>

// --- STB image load (declaration only — implementation in Texture.cpp) ---
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
struct TestVertex
{
	float posX, posY, posZ;
	float nrmX, nrmY, nrmZ;
	float uvX,  uvY;
};

// ---------------------------------------------------------------------------
// Golden image directory.
// ctest runs from build/debug/test/ → walk 3 levels to project root
// ---------------------------------------------------------------------------
static const char* kGoldenDir = "../../../test/render/golden/";

// ---------------------------------------------------------------------------
// Attachment list for golden comparison
// ---------------------------------------------------------------------------
static constexpr AttachmentName kGoldenAttachments[] = {
	AttachmentName::Position,
	AttachmentName::Normal,
	AttachmentName::Albedo,
	AttachmentName::MetallicRoughness,
	AttachmentName::HDRColor,
};

static constexpr int kGoldenAttachmentCount = static_cast<int>(std::size(kGoldenAttachments));

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class DeferredShadingTest : public VulkanTestFixture
{
protected:
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;

	void SetUp() override
	{
		VulkanTestFixture::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		if (pd.getProperties().limits.maxPushConstantsSize < sizeof(PushConstants))
		{
			m_hasVulkan = false;
			return;
		}

		// --- Render pass infrastructure ---
		m_attachmentManager = std::make_unique<AttachmentManager>(*m_device, pd);
		m_attachmentManager->Create({kRenderWidth, kRenderHeight});
		m_renderPassManager = std::make_unique<RenderPassManager>();

		// --- Geometry pass ---
		m_geometryPass = std::make_unique<GeometryPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			*m_attachmentManager,
			*m_renderPassManager,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv),
			gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		// --- Lighting pass ---
		m_lightingPass = std::make_unique<LightingPass>(
			*m_device, pd,
			*m_attachmentManager,
			2u,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
	}

	void TearDown() override
	{
		VulkanTestFixture::TearDown();
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

	// --- Golden image comparison ---

	/**
	 * @brief Loads a PNG into RGBA8 pixels, returns true on success.
	 */
	static bool LoadPng(const std::string& path,
	                    std::vector<uint8_t>& pixels,
	                    int& width, int& height, int& channels)
	{
		int w, h, c;
		unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);  // force RGBA
		if (!data) return false;

		width = w;
		height = h;
		channels = 4;
		const size_t byteCount = static_cast<size_t>(w) * h * 4;
		pixels.assign(data, data + byteCount);
		stbi_image_free(data);
		return true;
	}

	/**
	 * @brief Compares two RGBA8 images pixel‑wise within a tolerance.
	 * @return Number of differing pixels (0 = identical).
	 */
	static int ComparePixels(const std::vector<uint8_t>& a,
	                         const std::vector<uint8_t>& b,
	                         int width, int height,
	                         int maxDiffPerChannel = 2)
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

	/**
	 * @brief Returns the golden PNG path for a given attachment name.
	 */
	static std::string GoldenPath(AttachmentName name)
	{
		return std::string(kGoldenDir) + AttachmentNameToString(name) + ".png";
	}

	// --- Render pass infrastructure ---
	std::unique_ptr<AttachmentManager>  m_attachmentManager;
	std::unique_ptr<RenderPassManager>  m_renderPassManager;
	std::unique_ptr<GeometryPass>       m_geometryPass;
	std::unique_ptr<LightingPass>       m_lightingPass;
};

// ===========================================================================
// Golden Image Regression Test
// ===========================================================================

TEST_F(DeferredShadingTest, GbufferAttachments_MatchGoldenImages)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const auto& pd = m_physicalDevices[m_selectedPdIndex];

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

	// Copy vertex/index data (meshData shared into Mesh later)
	std::vector<float>    srcVertexData = rawMesh.dataArray;
	std::vector<uint32_t> srcIndexData  = rawMesh.indexArray;

	// -------------------------------------------------------------------
	// Step 2: Create camera (same as default scene: pos (0,2,5), target origin)
	// -------------------------------------------------------------------
	auto camera = std::make_shared<Camera>(
		static_cast<float>(kRenderWidth),
		static_cast<float>(kRenderHeight),
		60.0f, 0.1f, 100.0f);
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	const CameraUBOData camUBO = ComputeCameraUBO(*camera);

	// -------------------------------------------------------------------
	// Step 3: Create point light
	// -------------------------------------------------------------------
	auto light = std::make_shared<Light>(LightType::POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
	light->light_radius = 0.05f;

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
	std::vector<TestVertex> vertices(srcVertexCount);
	for (size_t i = 0; i < srcVertexCount; ++i)
	{
		const float* s = &srcVertexData[i * 14];
		TestVertex& v = vertices[i];
		v.posX = s[0]; v.posY = s[1]; v.posZ = s[2];
		v.nrmX = s[3]; v.nrmY = s[4]; v.nrmZ = s[5];
		v.uvX  = s[6]; v.uvY  = s[7];
	}

	std::vector<uint32_t> indices = srcIndexData;

	// -------------------------------------------------------------------
	// Step 6: Upload vertex + index buffers to GPU
	// -------------------------------------------------------------------
	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 vertices.data(), vertices.size() * sizeof(TestVertex),
	                 sizeof(TestVertex), static_cast<uint32_t>(vertices.size()));

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
	// Step 7: Transition G-Buffer attachments & record geometry pass
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
	// Step 8: Upload light SSBO & record lighting pass
	// -------------------------------------------------------------------
	const auto& lightPos = light->GetPosition();
	PointLightGpu gpuLight = {};
	gpuLight.posX   = lightPos.x;
	gpuLight.posY   = lightPos.y;
	gpuLight.posZ   = lightPos.z;
	gpuLight.colorR = light->light_color.r;
	gpuLight.colorG = light->light_color.g;
	gpuLight.colorB = light->light_color.b;
	gpuLight.power  = light->light_power;
	gpuLight.radius = light->light_radius;

	auto lightSSBO = std::make_unique<VulkanBuffer>(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		sizeof(PointLightGpu),
		vk::BufferUsageFlagBits::eStorageBuffer |
		    vk::BufferUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal);
	lightSSBO->Upload(&gpuLight, sizeof(PointLightGpu));

	{
		auto& cmd = BeginCmd();
		m_lightingPass->Record(*cmd, *lightSSBO, 1,
		                       camera->GetPosition(),
		                       camUBO.view,
		                       {kRenderWidth, kRenderHeight},
		                       0);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 9: Capture attachment screenshots & compare with golden images
	// -------------------------------------------------------------------
	const std::string tmpDir = std::string(kGoldenDir);

	bool allGoldenExist = true;
	for (int i = 0; i < kGoldenAttachmentCount; ++i)
	{
		if (!std::ifstream(GoldenPath(kGoldenAttachments[i])).good())
		{
			allGoldenExist = false;
			break;
		}
	}

	// Create golden directory if needed
	{
		std::filesystem::create_directories(kGoldenDir);
	}

	for (int i = 0; i < kGoldenAttachmentCount; ++i)
	{
		const AttachmentName name = kGoldenAttachments[i];
		const bool isNormal = (name == AttachmentName::Normal);

		// Screenshot path (temporary, next to golden)
		const std::string tmpPath = tmpDir + AttachmentNameToString(name) + ".tmp.png";

		VulkanImage& attachment = m_attachmentManager->GetAttachment(name);
		const bool captured = Screenshot::CaptureAttachment(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			attachment, tmpPath, isNormal);

		ASSERT_TRUE(captured) << "Failed to capture attachment: "
		                      << AttachmentNameToString(name);

		const std::string goldenPath = GoldenPath(name);

		if (!allGoldenExist)
		{
			// First run — rename .tmp.png → .png to generate golden image
			std::rename(tmpPath.c_str(), goldenPath.c_str());
		}
		else
		{
			// Compare against golden image
			std::vector<uint8_t> tmpPixels, goldenPixels;
			int tmpW, tmpH, tmpC, goldW, goldH, goldC;

			const bool tmpLoaded = LoadPng(tmpPath, tmpPixels, tmpW, tmpH, tmpC);
			const bool goldLoaded = LoadPng(goldenPath, goldenPixels, goldW, goldH, goldC);

			ASSERT_TRUE(tmpLoaded) << "Failed to load captured PNG: " << tmpPath;
			ASSERT_TRUE(goldLoaded) << "Failed to load golden PNG: " << goldenPath;
			ASSERT_EQ(tmpW, goldW) << "Width mismatch for " << AttachmentNameToString(name);
			ASSERT_EQ(tmpH, goldH) << "Height mismatch for " << AttachmentNameToString(name);

			const int badPixels = ComparePixels(tmpPixels, goldenPixels, tmpW, tmpH, 2);

			// Clean up temp file
			std::remove(tmpPath.c_str());

			EXPECT_EQ(badPixels, 0)
				<< badPixels << " pixel(s) differ in " << AttachmentNameToString(name)
				<< " (threshold: 2 per channel).";
		}
	}

	if (!allGoldenExist)
	{
		GTEST_SKIP() << "Golden images generated.  Re-run the test to compare.";
	}
}
