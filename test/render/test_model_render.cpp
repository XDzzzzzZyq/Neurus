/**
 * @file test_model_render.cpp
 * @brief End-to-end GPU test: load OBJ + texture, render G-Buffer → PBR lighting, verify output.
 *
 * Validates the full deferred rendering pipeline:
 *   - Loads a real OBJ model (sphere.obj) via MeshData
 *   - Loads a texture (BAKED.png) via Texture::FromFile
 *   - Creates Material with albedo texture and default PBR params
 *   - Creates Camera, PointLight, Scene with registration
 *   - Runs G-Buffer geometry pass (GeometryPass)
 *   - Runs PBR lighting compute pass (LightingPass)
 *   - Reads back HDR colour output via staging buffer
 *   - Verifies at least one pixel has non-zero RGB value
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 * @note Asset paths are relative to the test executable directory (build/debug/).
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"

// Render layer
#include "render/AttachmentManager.h"
#include "render/GeometryPass.h"
#include "render/LightingPass.h"
#include "render/Material.h"
#include "render/RenderPassManager.h"
#include "render/Texture.h"
#include "asset/ImageData.h"
#include "render/Texture.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/BufferLayout.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

// Data layer
#include "asset/MeshData.h"

// Scene layer
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

// Embedded shaders
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <pbr_lighting.comp.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <memory>
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
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @brief GPU test fixture for model rendering through G-Buffer → PBR lighting.
 *
 * Creates a headless Vulkan device, G-Buffer + HDR colour attachments,
 * GeometryPass, LightingPass, Camera, Light, and Scene.
 */
class ModelRenderTest : public VulkanTestShared
{
protected:
	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		// --- Check push-constant size support ---
		const auto& limits = pd.getProperties().limits;
		if (limits.maxPushConstantsSize < sizeof(PushConstants))
		{
			m_hasVulkan = false;
			return;
		}

		// --- Attachment manager (G-Buffer + HDR color + depth) ---
		m_attachmentManager = std::make_unique<AttachmentManager>(*m_device, pd);
		m_attachmentManager->Create({kRenderWidth, kRenderHeight});

		// --- Render pass manager ---
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
			2u,                          // numSets = kMaxFramesInFlight
			m_queue, m_graphicsQueueFamily,
			pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));
	}

	/**
	 * @brief Converts an IEEE 754 half-float (16-bit) to a 32-bit float.
	 */
	static float HalfToFloat(uint16_t half)
	{
		const uint32_t h = half;
		const uint32_t sign     = (h >> 15) & 0x0001;
		const uint32_t exp      = (h >> 10) & 0x001F;
		const uint32_t mantissa =  h        & 0x03FF;

		uint32_t f32;

		if (exp == 0)
		{
			if (mantissa == 0)
			{
				f32 = sign << 31;
			}
			else
			{
				int e = -14;
				uint32_t m = mantissa;
				while ((m & 0x0400) == 0)
				{
					m <<= 1;
					--e;
				}
				m &= 0x03FF;
				f32 = (sign << 31) | ((uint32_t)(e + 127) << 23) | (m << 13);
			}
		}
		else if (exp == 0x1F)
		{
			f32 = (sign << 31) | (0xFFu << 23) | (mantissa << 13);
		}
		else
		{
			f32 = (sign << 31) | ((uint32_t)(exp + 112) << 23) | (mantissa << 13);
		}

		float result;
		std::memcpy(&result, &f32, sizeof(float));
		return result;
	}

	/**
	 * @brief Creates a CameraUBOData from a neurus::Camera object.
	 */
	static CameraUBOData MakeCameraUBO(Camera& cam)
	{
		CameraUBOData ubo;
		ubo.view = cam.GetViewMatrix();
		ubo.viewProj = cam.GetProjectionMatrix() * ubo.view;
		return ubo;
	}

	/**
	 * @brief Reads back HDR colour output into a float array.
	 *
	 * Assumes RGBA16F format (8 bytes per pixel). Converts half-floats to
	 * single-precision floats.
	 */
	std::vector<float> ReadbackHdrOutput()
	{
		auto& pd = PhysicalDevice();
		const vk::DeviceSize imageByteSize = kRenderWidth * kRenderHeight * 8; // RGBA16F

		// Staging buffer: host-visible, TRANSFER_DST
		VulkanBuffer stagingBuf(*m_device, pd, m_queue, m_graphicsQueueFamily,
		                        imageByteSize,
		                        vk::BufferUsageFlagBits::eTransferDst,
		                        vk::MemoryPropertyFlagBits::eHostVisible |
		                            vk::MemoryPropertyFlagBits::eHostCoherent);

		// Transition HDR output: GENERAL → TRANSFER_SRC_OPTIMAL
		{
			auto& cmd = BeginCmd();
			auto& hdrColor = m_attachmentManager->GetAttachment(AttachmentName::HDRColor);

			vk::ImageMemoryBarrier barrier(
				vk::AccessFlagBits::eShaderWrite,
				vk::AccessFlagBits::eTransferRead,
				vk::ImageLayout::eGeneral,
				vk::ImageLayout::eTransferSrcOptimal,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				*hdrColor.ImageHandle(),
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
				                          0, 1, 0, 1));

			cmd.pipelineBarrier(
				vk::PipelineStageFlagBits::eComputeShader,
				vk::PipelineStageFlagBits::eTransfer,
				{},
				{},
				{},
				{barrier});

			// Copy image → buffer
			vk::BufferImageCopy copyRegion(
				0, 0, 0,
				vk::ImageSubresourceLayers(
					vk::ImageAspectFlagBits::eColor, 0, 0, 1),
				vk::Offset3D(0, 0, 0),
				vk::Extent3D(kRenderWidth, kRenderHeight, 1));

			cmd.copyImageToBuffer(*hdrColor.ImageHandle(),
			                      vk::ImageLayout::eTransferSrcOptimal,
			                      stagingBuf.buffer(),
			                      {copyRegion});

			EndSubmitWait(cmd);
		}

		// Map, convert half-float → float
		const uint32_t pixelCount = kRenderWidth * kRenderHeight;
		std::vector<float> result(pixelCount * 4);
		void* mapped = stagingBuf.Map();
		const auto* src = static_cast<const uint16_t*>(mapped);
		for (size_t i = 0; i < pixelCount * 4; ++i)
		{
			result[i] = HalfToFloat(src[i]);
		}
		stagingBuf.Unmap();

		return result;
	}

	/** Transition G-Buffer attachments to color attachment optimal. */
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

	// --- Constants ---
	static constexpr uint32_t kRenderWidth  = 256;
	static constexpr uint32_t kRenderHeight = 256;

	// --- Render pass infrastructure ---
	std::unique_ptr<AttachmentManager>  m_attachmentManager;
	std::unique_ptr<RenderPassManager>  m_renderPassManager;

	// --- Systems under test ---
	std::unique_ptr<GeometryPass>  m_geometryPass;
	std::unique_ptr<LightingPass>  m_lightingPass;
};

// ===========================================================================
// Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// 1. Constructor - pipeline is created successfully
// ---------------------------------------------------------------------------

TEST_F(ModelRenderTest, Constructor_CreatesValidPipelines)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NE(m_geometryPass, nullptr);
	ASSERT_NE(m_lightingPass, nullptr);
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 2. End-to-end: sphere OBJ → G-Buffer → PBR lighting → non-zero output
// ---------------------------------------------------------------------------

TEST_F(ModelRenderTest, SphereMeshWithPBR_ProducesNonZeroOutput)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	const auto& pd = m_physicalDevices[m_selectedPdIndex];

	// -----------------------------------------------------------------------
	// Step 1: Load the sphere OBJ via MeshData
	// -----------------------------------------------------------------------
	std::string objPath = ResolveAssetPath("res/obj/sphere.obj");

	auto meshData = std::make_shared<MeshData>();
	const bool loaded = meshData->LoadObj(objPath);
	ASSERT_TRUE(loaded) << "Failed to load OBJ: " << objPath;

	const auto& rawMesh = meshData->GetMeshData();
	const size_t srcVertexCount = rawMesh.dataArray.size() / 14;
	const size_t indexCount = rawMesh.indexArray.size();

	ASSERT_GT(srcVertexCount, 0u) << "OBJ has no vertices";
	ASSERT_GT(indexCount, 0u) << "OBJ has no indices";

	// Pre-extract vertex and index data (before meshData is shared into the Mesh)
	std::vector<float> srcVertexData = rawMesh.dataArray;       // copy (14 floats/vert)
	std::vector<uint32_t> srcIndexData = rawMesh.indexArray;    // copy

	// -----------------------------------------------------------------------
	// Step 2: Load BAKED.png texture (albedo)
	// -----------------------------------------------------------------------
	std::string texPath = ResolveAssetPath("res/tex/BAKED.png");

	Texture albedoTexture = Texture::FromFile(
		*m_device, pd, m_queue, m_graphicsQueueFamily,
		texPath.c_str(),
		vk::Format::eR8G8B8A8Srgb);

	// Texture loading may fail if file not found - warn but don't abort
	if (!albedoTexture.IsValid())
	{
		std::cerr << "[WARN] Failed to load texture: " << texPath
		          << " - continuing without albedo texture." << std::endl;
	}

	// -----------------------------------------------------------------------
	// Step 3: Create Material with albedo texture and default PBR params
	// -----------------------------------------------------------------------
	auto material = std::make_shared<Material>();
	material->mat_name = "TestMaterial";

	// Set default PBR params: metal=0, rough=0.5 (default from InitParamData)
	material->SetMatParam(Material::MAT_METAL, 0.0f);
	material->SetMatParam(Material::MAT_ROUGH, 0.5f);
	material->SetMatParam(Material::MAT_ALBEDO, glm::vec3(1.0f, 1.0f, 1.0f));

	if (albedoTexture.IsValid())
	{
			// Wrap in shared_ptr for Texture::TextureRes
		auto texRes = std::make_shared<Texture>(std::move(albedoTexture));
		material->SetMatParam(Material::MAT_ALBEDO, texRes);
	}

	// -----------------------------------------------------------------------
	// Step 4: Create Camera (position above, looking at origin)
	// -----------------------------------------------------------------------
	auto camera = std::make_shared<Camera>(
		800.0f, 600.0f,  // viewport size
		60.0f,            // FOV
		0.1f,             // near
		100.0f);          // far

	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	// -----------------------------------------------------------------------
	// Step 5: Create PointLight at position (3, 3, 3) with white color
	// -----------------------------------------------------------------------
	auto light = std::make_shared<Light>(
		LightType::POINTLIGHT,
		10.0f,
		glm::vec3(1.0f, 1.0f, 1.0f));

	light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
	light->light_radius = 0.05f;

	// -----------------------------------------------------------------------
	// Step 6: Create Mesh from loaded data, assign material
	// -----------------------------------------------------------------------
	auto mesh = std::make_shared<Mesh>();
	mesh->o_name = "SphereMesh";
	mesh->o_mesh = meshData;  // share ownership (no move - we copied data above)
	mesh->o_material = material;

	// -----------------------------------------------------------------------
	// Step 7: Create Scene, register camera, mesh, light
	// -----------------------------------------------------------------------
	Scene scene;
	scene.UseCamera(camera);
	scene.UseMesh(mesh);
	scene.UseLight(light);

	// -----------------------------------------------------------------------
	// Step 8: Build CameraUBOData from the Camera
	// -----------------------------------------------------------------------
	const CameraUBOData camUBO = MakeCameraUBO(*camera);

	// -----------------------------------------------------------------------
	// Step 9: Extract vertex data and upload to GPU
	//
	// MeshData stores 14 floats per vertex:
	//   pos(3) + normal(3) + uv(2) + tangent(3) + bitangent(3)
	// GeometryPass expects 8 floats per vertex (pos + normal + uv) at stride 32.
	// We extract the first 8 floats from each vertex.
	// -----------------------------------------------------------------------
	std::vector<TestVertex> vertices(srcVertexCount);
	for (size_t i = 0; i < srcVertexCount; ++i)
	{
		const float* src = &srcVertexData[i * 14];
		TestVertex& v = vertices[i];
		v.posX = src[0]; v.posY = src[1]; v.posZ = src[2];
		v.nrmX = src[3]; v.nrmY = src[4]; v.nrmZ = src[5];
		v.uvX  = src[6]; v.uvY  = src[7];
	}

	// Copy index data
	std::vector<uint32_t> indices = srcIndexData;

	// --- Upload to GPU ---
	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 vertices.data(),
	                 vertices.size() * sizeof(TestVertex),
	                 sizeof(TestVertex),
	                 static_cast<uint32_t>(vertices.size()));

	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                indices.data(),
	                indices.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(indices.size()));

	// -----------------------------------------------------------------------
	// Step 10: Build GeometryRenderItem with identity model matrix
	// -----------------------------------------------------------------------
	GeometryRenderItem renderItem;
	renderItem.vertexBuffer = vbo.buffer();
	renderItem.indexBuffer  = ibo.buffer();
	renderItem.indexCount   = ibo.GetIndexCount();
	renderItem.indexType    = ibo.GetIndexType();
	renderItem.pushConstants.model = glm::mat4(1.0f);
	renderItem.pushConstants.normalMatrix = glm::mat4(1.0f);

	// -----------------------------------------------------------------------
	// Step 11: Transition G-Buffer attachments to renderable layouts
	// -----------------------------------------------------------------------
	TransitionGbufferToColorAttachment();

	// -----------------------------------------------------------------------
	// Step 12: Record geometry pass (G-Buffer write)
	// -----------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, camUBO,
		                       { renderItem },
		                       { kRenderWidth, kRenderHeight });
		EndSubmitWait(cmd);
	}

	// -----------------------------------------------------------------------
	// Step 13: Upload light to LightingPass and record lighting pass
	// -----------------------------------------------------------------------
	m_lightingPass->UploadLights(scene);

	// Record lighting compute pass
	{
		auto& cmd = BeginCmd();

		m_lightingPass->Record(*cmd,
		                       camera->GetPosition(),                 // camera world pos
		                       camUBO.view,                           // view matrix
		                       {kRenderWidth, kRenderHeight},
		                       0);                                     // frame index

		EndSubmitWait(cmd);
	}

	// -----------------------------------------------------------------------
	// Step 14: Read back HDR colour output
	// -----------------------------------------------------------------------
	std::vector<float> hdrPixels = ReadbackHdrOutput();

	// -----------------------------------------------------------------------
	// Step 15: Verify at least one pixel has non-zero RGB value
	// -----------------------------------------------------------------------
	bool foundNonZero = false;
	for (size_t i = 0; i < hdrPixels.size(); i += 4)
	{
		const float r = hdrPixels[i + 0];
		const float g = hdrPixels[i + 1];
		const float b = hdrPixels[i + 2];

		if (r > 0.01f || g > 0.01f || b > 0.01f)
		{
			foundNonZero = true;
			break;
		}
	}

	EXPECT_TRUE(foundNonZero)
		<< "No non-zero pixel found in HDR output after deferred PBR pipeline. "
		<< "The sphere should be illuminated by the point light at ("
		<< light->GetPosition().x << ", " << light->GetPosition().y
		<< ", " << light->GetPosition().z << ") with power "
		<< light->light_power << ".";

	if (!foundNonZero)
	{
		// Dump center pixel and a few surrounding for debugging
		const size_t cx = kRenderWidth / 2;
		const size_t cy = kRenderHeight / 2;

		for (int dy = -2; dy <= 2; ++dy)
		{
			for (int dx = -2; dx <= 2; ++dx)
			{
				const size_t px = cx + dx;
				const size_t py = cy + dy;
				if (px < kRenderWidth && py < kRenderHeight)
				{
					const size_t idx = (py * kRenderWidth + px) * 4;
					std::cerr << "  Pixel(" << px << "," << py << "): RGBA("
					          << hdrPixels[idx + 0] << ", "
					          << hdrPixels[idx + 1] << ", "
					          << hdrPixels[idx + 2] << ", "
					          << hdrPixels[idx + 3] << ")" << std::endl;
				}
			}
		}
	}
}
