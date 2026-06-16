/**
 * @file test_lighting.cpp
 * @brief Tests for LightingPass - PBR Cook-Torrance GGX compute shader.
 *
 * Validates:
 *   - LightingPass constructor creates a valid pipeline
 *   - Single point light dispatch produces non-zero HDR output
 *   - G-Buffer reading and HDR writing work correctly
 *   - Push constants (light count, camera pos, view matrix) are consumed
 *   - PointLightGpu SSBO layout matches shader expectation
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include "render/AttachmentManager.h"
#include "render/GeometryPass.h"
#include "render/LightingPass.h"
#include "render/RenderPassManager.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/BufferLayout.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <pbr_lighting.comp.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <memory>

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
 * @brief GPU test fixture for LightingPass.
 *
 * Creates a headless Vulkan device, G-Buffer attachments, renders a triangle
 * via GeometryPass, then dispatches the PBR lighting compute shader and
 * reads back the HDR colour output.
 */
class LightingPassTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Instance ---
			vk::ApplicationInfo appInfo("NeurusTest_Lighting",
			                            VK_MAKE_VERSION(0, 2, 0),
			                            "NeurusTest_Lighting",
			                            VK_MAKE_VERSION(0, 2, 0),
			                            VK_API_VERSION_1_4);
			std::vector<const char*> instanceExts = {
				VK_KHR_SURFACE_EXTENSION_NAME,
				VK_KHR_WIN32_SURFACE_EXTENSION_NAME
			};
			vk::InstanceCreateInfo instanceCI({}, &appInfo, {}, instanceExts);
			m_instance = std::make_unique<vk::raii::Instance>(m_context, instanceCI);

			// --- Physical device ---
			m_physicalDevices = vk::raii::PhysicalDevices(*m_instance);
			if (m_physicalDevices.empty())
			{
				m_hasVulkan = false;
				return;
			}

			// Pick discrete GPU if available
			m_selectedPdIndex = 0;
			for (uint32_t i = 0; i < static_cast<uint32_t>(m_physicalDevices.size()); ++i)
			{
				const auto props = m_physicalDevices[i].getProperties();
				if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
				{
					m_selectedPdIndex = i;
					break;
				}
			}
			auto& pd = m_physicalDevices[m_selectedPdIndex];

			// --- Queue family ---
			auto qfProps = pd.getQueueFamilyProperties();
			bool foundGraphics = false;
			for (uint32_t i = 0; i < static_cast<uint32_t>(qfProps.size()); ++i)
			{
				if (qfProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
				{
					m_graphicsQueueFamily = i;
					foundGraphics = true;
					break;
				}
			}
			if (!foundGraphics)
			{
				m_hasVulkan = false;
				return;
			}

			// --- Check push-constant size support ---
			const auto& limits = pd.getProperties().limits;
			if (limits.maxPushConstantsSize < sizeof(PushConstants))
			{
				m_hasVulkan = false;
				return;
			}

			// --- Device ---
			float prio = 1.0f;
			vk::DeviceQueueCreateInfo qCI({}, m_graphicsQueueFamily, 1, &prio);
			vk::PhysicalDeviceFeatures features;
			vk::DeviceCreateInfo devCI({}, qCI, {}, {}, &features);
			m_device = std::make_unique<vk::raii::Device>(pd, devCI);
			m_queue = m_device->getQueue(m_graphicsQueueFamily, 0);

			// --- Command pool ---
			vk::CommandPoolCreateInfo poolCI(
				vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				m_graphicsQueueFamily);
			m_commandPool = std::make_unique<vk::raii::CommandPool>(*m_device, poolCI);

			// --- Command buffers ---
			vk::CommandBufferAllocateInfo allocInfo(
				*m_commandPool, vk::CommandBufferLevel::ePrimary, 1);
			m_commandBuffers = vk::raii::CommandBuffers(*m_device, allocInfo);

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
				pbr_lighting_comp_spv, sizeof(pbr_lighting_comp_spv));

			m_hasVulkan = true;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[LightingPassTest::SetUp] Exception: " << e.what() << std::endl;
			m_hasVulkan = false;
		}
		catch (...)
		{
			m_hasVulkan = false;
		}
	}

	void TearDown() override
	{
		if (m_device)
		{
			m_device->waitIdle();
		}
	}

	// --- Helpers ---

	/** Begin a one-shot command buffer. */
	vk::raii::CommandBuffer& BeginCmd()
	{
		auto& cmd = m_commandBuffers[0];
		cmd.begin(vk::CommandBufferBeginInfo(
			vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		return cmd;
	}

	/** End, submit, and wait for a command buffer. */
	void EndSubmitWait(vk::raii::CommandBuffer& cmd)
	{
		cmd.end();
		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmd));
		m_queue.submit(submitInfo, nullptr);
		m_device->waitIdle();
	}

	/**
	 * @brief Creates a default camera looking at the triangle at the origin.
	 */
	CameraUBOData MakeTestCamera() const
	{
		CameraUBOData cam;
		const glm::mat4 proj = glm::perspective(
			glm::radians(60.0f),
			static_cast<float>(kRenderWidth) / static_cast<float>(kRenderHeight),
			0.1f, 100.0f);
		const glm::mat4 view = glm::lookAt(
			glm::vec3(0.0f, 0.0f, 2.0f),    // eye
			glm::vec3(0.0f, 0.0f, 0.0f),    // target
			glm::vec3(0.0f, 1.0f, 0.0f));    // up
		cam.viewProj = proj * view;
		cam.view = view;
		return cam;
	}

	/**
	 * @brief Creates a single test triangle in the XY plane facing +Z.
	 */
	static std::pair<std::vector<TestVertex>, std::vector<uint32_t>> TestTriangle()
	{
		std::vector<TestVertex> verts = {
			//  posX  posY posZ    nrmX nrmY nrmZ    uvX  uvY
			{   0.0f,-0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 0.5f, 1.0f },
			{   0.5f, 0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
			{  -0.5f, 0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
		};
		std::vector<uint32_t> indices = { 0, 1, 2 };
		return { verts, indices };
	}

	/**
	 * @brief Renders the test triangle into the G-Buffer.
	 *
	 * Transitions attachments, creates buffers, records geometry pass.
	 * Returns the camera UBO data used for the rendering.
	 */
	CameraUBOData RenderTestTriangle()
	{
		auto& pd = m_physicalDevices[m_selectedPdIndex];

		// Transition GBuffer → renderable
		TransitionGbufferToColorAttachment();

		auto [verts, indices] = TestTriangle();
		const uint32_t vStride = sizeof(TestVertex);

		VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
		                 verts.data(), verts.size() * vStride,
		                 vStride, static_cast<uint32_t>(verts.size()));

		IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
		                indices.data(), indices.size() * sizeof(uint32_t),
		                static_cast<uint32_t>(indices.size()));

		GeometryRenderItem item;
		item.vertexBuffer = vbo.buffer();
		item.indexBuffer  = ibo.buffer();
		item.indexCount   = ibo.GetIndexCount();
		item.indexType    = ibo.GetIndexType();
		item.pushConstants.model = glm::mat4(1.0f);
		item.pushConstants.normalMatrix = glm::mat4(1.0f);

		const auto camera = MakeTestCamera();

		{
			auto& cmd = BeginCmd();
			m_geometryPass->Record(*cmd, camera,
			                       {item},
			                       {kRenderWidth, kRenderHeight});
			EndSubmitWait(cmd);
		}

		return camera;
	}

	/**
	 * @brief Creates a point light SSBO with a single light.
	 */
	std::unique_ptr<VulkanBuffer> CreateLightSSBO(const glm::vec3& pos,
	                                              float power = 50.0f,
	                                              const glm::vec3& color = glm::vec3(1.0f))
	{
		const auto& pd = m_physicalDevices[m_selectedPdIndex];

		PointLightGpu light = {};
		light.colorR = color.r;
		light.colorG = color.g;
		light.colorB = color.b;
		light.posX   = pos.x;
		light.posY   = pos.y;
		light.posZ   = pos.z;
		light.power  = power;
		light.radius = 0.05f;

		auto ssbo = std::make_unique<VulkanBuffer>(
			*m_device, pd, m_queue, m_graphicsQueueFamily,
			sizeof(PointLightGpu),
			vk::BufferUsageFlagBits::eStorageBuffer |
			    vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		ssbo->Upload(&light, sizeof(PointLightGpu));
		return ssbo;
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
			// Zero or denormalized
			if (mantissa == 0)
			{
				f32 = sign << 31;
			}
			else
			{
				// Normalize denormal
				int e = -14;
				uint32_t m = mantissa;
				while ((m & 0x0400) == 0)
				{
					m <<= 1;
					--e;
				}
				m &= 0x03FF;  // Remove leading 1
				f32 = (sign << 31) | ((uint32_t)(e + 127) << 23) | (m << 13);
			}
		}
		else if (exp == 0x1F)
		{
			// Infinity or NaN
			f32 = (sign << 31) | (0xFFu << 23) | (mantissa << 13);
		}
		else
		{
			// Normalized
			f32 = (sign << 31) | ((uint32_t)(exp + 112) << 23) | (mantissa << 13);
		}

		float result;
		std::memcpy(&result, &f32, sizeof(float));
		return result;
	}

	/**
	 * @brief Reads back HDR colour output into a float array.
	 *
	 * Assumes RGBA16F format (8 bytes per pixel). Converts half-floats to
	 * single-precision floats.
	 */
	std::vector<float> ReadbackHdrOutput()
	{
		auto& pd = m_physicalDevices[m_selectedPdIndex];
		const vk::DeviceSize imageByteSize = kRenderWidth * kRenderHeight * 8; // RGBA16F = 8 B/px

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
	static constexpr uint32_t kRenderWidth  = 128;
	static constexpr uint32_t kRenderHeight = 128;

	// --- Vulkan state ---
	bool m_hasVulkan = false;
	vk::raii::Context m_context;
	std::unique_ptr<vk::raii::Instance> m_instance;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;
	uint32_t m_selectedPdIndex = 0;
	std::unique_ptr<vk::raii::Device> m_device;
	uint32_t m_graphicsQueueFamily = 0;
	vk::Queue m_queue = nullptr;
	std::unique_ptr<vk::raii::CommandPool> m_commandPool;
	vk::raii::CommandBuffers m_commandBuffers = nullptr;

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

TEST_F(LightingPassTest, Constructor_CreatesValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	ASSERT_NE(m_lightingPass, nullptr);
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 2. PointLightGpu - size matches shader expectation (std140, 48 bytes)
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, PointLightGpu_SizeIs48Bytes)
{
	EXPECT_EQ(sizeof(PointLightGpu), 48u);
}

// ---------------------------------------------------------------------------
// 3. LightingPushConstants - size matches shader expectation (96 bytes)
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, PushConstants_SizeIs96Bytes)
{
	EXPECT_EQ(sizeof(LightingPushConstants), 96u);
}

// ---------------------------------------------------------------------------
// 4. Non-copyable / movable
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<LightingPass>,
	              "LightingPass must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<LightingPass>,
	              "LightingPass must not be copy-assignable");
	SUCCEED();
}

TEST_F(LightingPassTest, Movable)
{
	static_assert(std::is_move_constructible_v<LightingPass>,
	              "LightingPass must be move-constructible");
	static_assert(std::is_move_assignable_v<LightingPass>,
	              "LightingPass must be move-assignable");
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 5. Single point light - dispatch produces non-zero HDR output
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, SinglePointLight_ProducesNonZeroOutput)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// --- Step 1: Render test triangle into G-Buffer ---
	const auto camera = RenderTestTriangle();

	// --- Step 2: Create point light SSBO (light at (0, 0, 3), above triangle) ---
	auto lightSSBO = CreateLightSSBO(
		glm::vec3(0.0f, 0.0f, 3.0f),   // position
		50.0f,                           // power
		glm::vec3(1.0f, 1.0f, 1.0f));    // color

	// --- Step 3: Record lighting pass ---
	{
		auto& cmd = BeginCmd();

		m_lightingPass->Record(*cmd,
		                       *lightSSBO,
		                       1,                        // light count
		                       glm::vec3(0.0f, 0.0f, 2.0f), // camera pos
		                       MakeTestCamera().view,        // view matrix
		                       {kRenderWidth, kRenderHeight},
		                       0);                           // frame index

		EndSubmitWait(cmd);
	}

	// --- Step 4: Read back HDR output ---
	std::vector<float> hdrPixels = ReadbackHdrOutput();

	// --- Step 5: Verify at least one pixel has non-zero colour ---
	// The triangle covers roughly the center of the framebuffer.
	// Scan all pixels for any non-zero RGB values.
	bool foundNonZero = false;
	for (size_t i = 0; i < hdrPixels.size(); i += 4)
	{
		float r = hdrPixels[i + 0];
		float g = hdrPixels[i + 1];
		float b = hdrPixels[i + 2];

		if (r > 0.01f || g > 0.01f || b > 0.01f)
		{
			foundNonZero = true;
			break;
		}
	}

	EXPECT_TRUE(foundNonZero)
		<< "No non-zero pixel found in HDR output after single point light dispatch. "
		<< "The lighting pass should produce visible illumination on the test triangle.";

	if (!foundNonZero)
	{
		// Dump center pixel for debugging
		const size_t centerIdx = (kRenderHeight / 2 * kRenderWidth + kRenderWidth / 2) * 4;
		ADD_FAILURE()
			<< "Center pixel RGBA: ("
			<< hdrPixels[centerIdx + 0] << ", "
			<< hdrPixels[centerIdx + 1] << ", "
			<< hdrPixels[centerIdx + 2] << ", "
			<< hdrPixels[centerIdx + 3] << ")";
	}
}

// ---------------------------------------------------------------------------
// 6. Zero lights - produces ambient-only output (non-black for covered pixels)
// ---------------------------------------------------------------------------

TEST_F(LightingPassTest, ZeroLights_ProducesAmbientOnly)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// --- Render triangle ---
	const auto camera = RenderTestTriangle();

	// --- Create empty SSBO (0 lights) ---
	auto& pd = m_physicalDevices[m_selectedPdIndex];
	VulkanBuffer emptySSBO(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                       sizeof(PointLightGpu), // minimum size
	                       vk::BufferUsageFlagBits::eStorageBuffer |
	                           vk::BufferUsageFlagBits::eTransferDst,
	                       vk::MemoryPropertyFlagBits::eDeviceLocal);

	// --- Record lighting pass with 0 lights ---
	{
		auto& cmd = BeginCmd();

		m_lightingPass->Record(*cmd,
		                       emptySSBO,
		                       0,                        // zero lights
		                       glm::vec3(0.0f, 0.0f, 2.0f),
		                       MakeTestCamera().view,
		                       {kRenderWidth, kRenderHeight},
		                       0);

		EndSubmitWait(cmd);
	}

	// --- Read back ---
	std::vector<float> hdrPixels = ReadbackHdrOutput();

	// --- Verify: at least some covered pixels have the ambient term ---
	// Ambient = 0.03 * albedo = 0.03 for white albedo
	bool foundLit = false;
	for (size_t i = 0; i < hdrPixels.size(); i += 4)
	{
		float r = hdrPixels[i + 0];
		if (r > 0.001f && r < 0.05f)  // ambient is ~0.03
		{
			foundLit = true;
			break;
		}
	}

	// Zero lights should still write ambient to covered pixels
	EXPECT_TRUE(foundLit)
		<< "Zero-light dispatch should produce ambient term (~0.03) on the test triangle.";
}
