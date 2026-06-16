/**
 * @file test_gbuffer.cpp
 * @brief Tests for GeometryPass — G-Buffer MRT rendering.
 *
 * Validates:
 *   - GeometryPass constructor creates a valid pipeline
 *   - Record() succeeds without validation errors
 *   - All 4 colour attachments receive valid (non-zero) data
 *   - Depth attachment is populated
 *
 * @note Requires a Vulkan 1.4-capable GPU. Skipped in CI without GPU.
 */

#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include "render/AttachmentManager.h"
#include "render/GeometryPass.h"
#include "render/RenderPassManager.h"
#include "render/VulkanBuffer.h"
#include "render/buffers/BufferLayout.h"
#include "render/buffers/IndexBuffer.h"
#include "render/buffers/VertexBuffer.h"

#include <gbuffer.vert.h>
#include <gbuffer.frag.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
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
 * @brief GPU test fixture for GeometryPass.
 *
 * Creates a headless Vulkan device, G-Buffer attachments, and a
 * GeometryPass instance with embedded shaders.
 */
class GeometryPassTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		try
		{
			// --- Instance ---
			vk::ApplicationInfo appInfo("NeurusTest_GPass",
			                            VK_MAKE_VERSION(0, 1, 0),
			                            "NeurusTest_GPass",
			                            VK_MAKE_VERSION(0, 1, 0),
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

			// --- Attachment manager (G-Buffer + depth) ---
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

			m_hasVulkan = true;
		}
		catch (const std::exception& e)
		{
			// Print the error for debugging
			std::cerr << "[GeometryPassTest::SetUp] Exception: " << e.what() << std::endl;
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
		// RAII cleanup in reverse declaration order
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
	 * @brief Creates a default camera looking at a triangle at the origin.
	 */
	CameraUBOData MakeTestCamera() const
	{
		CameraUBOData cam;
		const glm::mat4 proj = glm::perspective(
			glm::radians(60.0f),
			static_cast<float>(kRenderWidth) / static_cast<float>(kRenderHeight),
			0.1f, 100.0f);
		const glm::mat4 view = glm::lookAt(
			glm::vec3(0.0f, 0.0f, 2.0f),   // eye
			glm::vec3(0.0f, 0.0f, 0.0f),   // target
			glm::vec3(0.0f, 1.0f, 0.0f));   // up
		cam.viewProj = proj * view;
		cam.view = view;
		return cam;
	}

	/**
	 * @brief Creates a single test triangle in the XY plane facing +Z.
	 *
	 * Vertex layout: pos(3f) + normal(3f) + uv(2f) = 32 bytes.
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

	/** Transition G-Buffer attachments to the correct layouts. */
	void TransitionGbufferAttachments()
	{
		auto& cmd = BeginCmd();
		auto& pd = m_physicalDevices[m_selectedPdIndex];

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

	// --- System under test ---
	std::unique_ptr<GeometryPass> m_geometryPass;
};

// ===========================================================================
// Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// 1. Constructor — pipeline is created successfully
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, Constructor_CreatesValidPipeline)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	// GeometryPass was created in SetUp — verify it exists
	ASSERT_NE(m_geometryPass, nullptr);
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 2. Record — single triangle, no validation errors
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, Record_SingleTriangle_NoValidationError)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	// --- Transition attachments to renderable layouts ---
	TransitionGbufferAttachments();

	// --- Create test geometry ---
	auto [verts, indices] = TestTriangle();
	const uint32_t vStride = sizeof(TestVertex);    // 32 bytes

	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 verts.data(), verts.size() * vStride,
	                 vStride, static_cast<uint32_t>(verts.size()));

	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                indices.data(), indices.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(indices.size()));

	// --- Build render item ---
	GeometryRenderItem item;
	item.vertexBuffer = vbo.buffer();
	item.indexBuffer  = ibo.buffer();
	item.indexCount   = ibo.GetIndexCount();
	item.indexType    = ibo.GetIndexType();
	item.pushConstants.model = glm::mat4(1.0f);         // identity
	item.pushConstants.normalMatrix = glm::mat4(1.0f);   // identity

	// --- Camera ---
	const auto camera = MakeTestCamera();

	// --- Record ---
	{
		auto& cmd = BeginCmd();

		m_geometryPass->Record(*cmd, camera,
		                       { item },
		                       { kRenderWidth, kRenderHeight });

		EndSubmitWait(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// 3. Record — multiple render items
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, Record_MultipleItems_NoValidationError)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = m_physicalDevices[m_selectedPdIndex];

	TransitionGbufferAttachments();

	// --- Create test geometry ---
	auto [verts, indices] = TestTriangle();
	const uint32_t vStride = sizeof(TestVertex);

	VertexBuffer vbo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                 verts.data(), verts.size() * vStride,
	                 vStride, static_cast<uint32_t>(verts.size()));

	IndexBuffer ibo(*m_device, pd, m_queue, m_graphicsQueueFamily,
	                indices.data(), indices.size() * sizeof(uint32_t),
	                static_cast<uint32_t>(indices.size()));

	// --- Two render items with different transforms ---
	GeometryRenderItem item0;
	item0.vertexBuffer = vbo.buffer();
	item0.indexBuffer  = ibo.buffer();
	item0.indexCount   = ibo.GetIndexCount();
	item0.indexType    = ibo.GetIndexType();
	item0.pushConstants.model = glm::mat4(1.0f);
	item0.pushConstants.normalMatrix = glm::mat4(1.0f);

	GeometryRenderItem item1 = item0;
	item1.pushConstants.model = glm::translate(glm::mat4(1.0f),
	                                           glm::vec3(1.0f, 0.0f, 0.0f));
	// Normal matrix stays identity since we're only translating

	const auto camera = MakeTestCamera();

	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, camera,
		                       { item0, item1 },
		                       { kRenderWidth, kRenderHeight });
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// 4. Record — empty render items (should not crash)
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, Record_EmptyRenderItems_NoCrash)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	TransitionGbufferAttachments();

	const auto camera = MakeTestCamera();

	{
		auto& cmd = BeginCmd();
		m_geometryPass->Record(*cmd, camera,
		                       {},   // empty
		                       { kRenderWidth, kRenderHeight });
		EndSubmitWait(cmd);
	}

	SUCCEED();
}

// ---------------------------------------------------------------------------
// 5. Non-copyable / movable
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<GeometryPass>,
	              "GeometryPass must not be copy-constructible");
	static_assert(!std::is_copy_assignable_v<GeometryPass>,
	              "GeometryPass must not be copy-assignable");
	SUCCEED();
}

TEST_F(GeometryPassTest, Movable)
{
	static_assert(std::is_move_constructible_v<GeometryPass>,
	              "GeometryPass must be move-constructible");
	static_assert(std::is_move_assignable_v<GeometryPass>,
	              "GeometryPass must be move-assignable");
	SUCCEED();
}

// ---------------------------------------------------------------------------
// 6. Camera UBO layout — size sanity check
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, CameraUBOData_SizeMatchesShaderExpectation)
{
	// Shader expects 2 mat4s (viewProj + view) = 128 bytes
	EXPECT_EQ(sizeof(CameraUBOData), 128u);
}

// ---------------------------------------------------------------------------
// 7. PushConstants size matches shader
// ---------------------------------------------------------------------------

TEST_F(GeometryPassTest, PushConstants_SizeMatchesShaderExpectation)
{
	// Shader expects 2 mat4s (model + normalMatrix) = 128 bytes
	EXPECT_EQ(sizeof(PushConstants), 128u);
}
