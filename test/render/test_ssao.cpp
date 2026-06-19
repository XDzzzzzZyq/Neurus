/**
 * @file test_ssao.cpp
 * @brief Reference-image regression test for the SSAO pass.
 *
 * Renders a known scene (sphere OBJ + point light) through geometry pass
 * and SSAO pass at 256×256, then captures the SSAO attachment as a PNG.
 * On first run the reference image does not exist — the test generates it
 * automatically and reports SKIPPED.  Subsequent runs compare pixel-wise
 * and FAIL on any pixel difference exceeding the allowed tolerance.
 *
 * @note Requires a Vulkan 1.4-capable GPU.  Skipped in CI without GPU.
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

// --- Render layer ---
#include "render/AttachmentManager.h"
#include "render/GeometryPass.h"
#include "render/Material.h"
#include "render/RenderPassManager.h"
#include "render/Screenshot.h"
#include "render/SSAOPass.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

// --- Data layer ---
#include "asset/MeshData.h"

// --- Scene layer ---
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"

// --- Embedded shaders ---
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <ssao.comp.h>

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
struct TestVertex
{
	float posX, posY, posZ;
	float nrmX, nrmY, nrmZ;
	float uvX,  uvY;
};

// ---------------------------------------------------------------------------
// Reference image directory
// ---------------------------------------------------------------------------
static const char* kReferenceDir = "../../../test/render/reference/";

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class SSAOTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

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

		// --- SSAO pass ---
		m_ssaoPass = std::make_unique<SSAOPass>(
			*m_device, pd,
			*m_attachmentManager,
			1u,   // one descriptor set for single-frame test
			m_queue, m_graphicsQueueFamily,
			ssao_comp_spv, sizeof(ssao_comp_spv));
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

	static std::string ReferencePath()
	{
		return std::string(kReferenceDir) + "ssao/SSAO.png";
	}

	// --- Render pass infrastructure ---
	std::unique_ptr<AttachmentManager>  m_attachmentManager;
	std::unique_ptr<RenderPassManager>  m_renderPassManager;
	std::unique_ptr<GeometryPass>       m_geometryPass;
	std::unique_ptr<SSAOPass>           m_ssaoPass;
};

// ===========================================================================
// SSAO Reference Image Regression Test
// ===========================================================================

TEST_F(SSAOTest, SSAOAttachment_MatchesReferenceImage)
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
	// Step 2: Create camera (default: pos (0,2,5), target origin)
	// -------------------------------------------------------------------
	auto camera = std::make_shared<Camera>(
		static_cast<float>(kRenderWidth),
		static_cast<float>(kRenderHeight),
		60.0f, 0.1f, 100.0f);
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	const CameraUBOData camUBO = ComputeCameraUBO(*camera);

	// -------------------------------------------------------------------
	// Step 3: Build mesh
	// -------------------------------------------------------------------
	auto material = std::make_shared<Material>();
	material->SetMatParam(Material::MAT_METAL, 0.0f);
	material->SetMatParam(Material::MAT_ROUGH, 0.5f);
	material->SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f, 1.0f, 1.0f));

	auto mesh = std::make_shared<Mesh>();
	mesh->o_mesh = meshData;
	mesh->o_material = material;

	// -------------------------------------------------------------------
	// Step 4: Convert vertex data (14 floats → 8: pos+normal+uv)
	// -------------------------------------------------------------------
	std::vector<TestVertex> vertices(srcVertexCount);
	for (size_t i = 0; i < srcVertexCount; ++i)
	{
		const float* s = &srcVertexData[i * 14];
		TestVertex& v = vertices[i];
		v.posX = s[0] * 0.25f; v.posY = s[1] * 0.25f; v.posZ = s[2] * 0.25f;
		v.nrmX = s[3]; v.nrmY = s[4]; v.nrmZ = s[5];
		v.uvX  = s[6]; v.uvY  = s[7];
	}

	std::vector<uint32_t> indices = srcIndexData;

	// -------------------------------------------------------------------
	// Step 5: Upload vertex + index buffers to GPU
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
	// Step 6: Transition G-Buffer & record geometry pass
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
	// Step 7: Run SSAO pass
	// -------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();

		const glm::mat4 viewProj = camUBO.viewProj;
		const glm::mat4 view     = camUBO.view;
		const glm::vec3 camPos   = camera->GetPosition();

		m_ssaoPass->UpdateParams(viewProj, view, camPos);
		m_ssaoPass->Record(*cmd, {kRenderWidth, kRenderHeight}, 0);

		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 8: Capture SSAO attachment & compare with reference
	// -------------------------------------------------------------------
	const std::string refPath = ReferencePath();
	const bool refExists = std::ifstream(refPath).good();

	// Create reference sub-directory if needed
	std::filesystem::create_directories(std::string(kReferenceDir) + "ssao/");

	const std::string tmpPath = refPath + ".tmp";

	Image& ssaoAttachment = m_attachmentManager->GetAttachment(AttachmentName::SSAO);
	const bool captured = Screenshot::CaptureAttachment(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		ssaoAttachment, tmpPath, false);  // not signed, so no remap

	ASSERT_TRUE(captured) << "Failed to capture SSAO attachment";

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

		const int badPixels = ComparePixels(tmpPixels, refPixels, tmpW, tmpH, 2);

		// Clean up temp file
		std::remove(tmpPath.c_str());

		EXPECT_EQ(badPixels, 0)
			<< badPixels << " pixel(s) differ in SSAO (threshold: 2 per channel).";
	}
}
