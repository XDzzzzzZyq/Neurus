/**
 * @file TestVulkanShared.cpp
 * @brief Implementation of the shared GPU test fixture base class.
 */

#include "TestVulkanShared.h"

#include "render/Image.h"

#include <iostream>

using namespace neurus;

// ===========================================================================
// SetUp — standard Vulkan bootstrap
// ===========================================================================

void VulkanTestShared::SetUp()
{
	try
	{
		// --- Instance ---
		vk::ApplicationInfo appInfo("NeurusTest",
		                            VK_MAKE_VERSION(0, 5, 0),
		                            "NeurusTest",
		                            VK_MAKE_VERSION(0, 5, 0),
		                            VK_API_VERSION_1_4);
		std::vector<const char*> instanceExts = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
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
		m_graphicsQueueFamily = UINT32_MAX;
		for (uint32_t i = 0; i < static_cast<uint32_t>(qfProps.size()); ++i)
		{
			if (qfProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
			{
				m_graphicsQueueFamily = i;
				break;
			}
		}
		if (m_graphicsQueueFamily == UINT32_MAX)
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

		m_hasVulkan = true;
	}
	catch (const std::exception& e)
	{
		std::cerr << "[VulkanTestShared::SetUp] " << e.what() << std::endl;
		m_hasVulkan = false;
	}
	catch (...)
	{
		m_hasVulkan = false;
	}
}

// ===========================================================================
// TearDown
// ===========================================================================

void VulkanTestShared::TearDown()
{
	if (m_device)
	{
		m_device->waitIdle();
	}
}

// ===========================================================================
// Command buffer helpers
// ===========================================================================

vk::raii::CommandBuffer& VulkanTestShared::BeginCmd()
{
	auto& cmd = m_commandBuffers[0];
	cmd.begin(
		vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	return cmd;
}

void VulkanTestShared::EndSubmitWait(vk::raii::CommandBuffer& cmd)
{
	cmd.end();
	vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmd));
	m_queue.submit(submitInfo, nullptr);
	m_device->waitIdle();
}

// ===========================================================================
// Asset path resolution
// ===========================================================================

std::string VulkanTestShared::ResolveAssetPath(const char* assetRelative)
{
	// Try: relative from build/debug/Debug/ (MSVC multi-config layout)
	std::string path1 = std::string("../../../") + assetRelative;
	{
		std::ifstream f(path1);
		if (f.good()) return path1;
	}
	// Try: relative from build/debug/ (single-config layout)
	std::string path2 = std::string("../../") + assetRelative;
	{
		std::ifstream f(path2);
		if (f.good()) return path2;
	}
	// Fallback: return the primary path and let the caller handle failure
	return path1;
}

// ===========================================================================
// HalfToFloat — IEEE 754 half-float conversion
// ===========================================================================

float VulkanTestShared::HalfToFloat(uint16_t half)
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

// ===========================================================================
// ComputeCameraUBO
// ===========================================================================

CameraUBOData VulkanTestShared::ComputeCameraUBO(Camera& cam)
{
	CameraUBOData ubo;
	ubo.view = cam.GetViewMatrix();
	ubo.viewProj = cam.GetProjectionMatrix() * ubo.view;
	return ubo;
}

// ===========================================================================
// MakeTestCamera
// ===========================================================================

CameraUBOData VulkanTestShared::MakeTestCamera(uint32_t width, uint32_t height)
{
	CameraUBOData cam;
	const glm::mat4 proj = glm::perspective(
		glm::radians(60.0f),
		static_cast<float>(width) / static_cast<float>(height),
		0.1f, 100.0f);
	const glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, 0.0f, 2.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	cam.viewProj = proj * view;
	cam.view = view;
	return cam;
}

// ===========================================================================
// TestTriangle
// ===========================================================================

std::pair<std::vector<TestVertex>, std::vector<uint32_t>> VulkanTestShared::TestTriangle()
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

// ===========================================================================
// ReadbackHdrOutput
// ===========================================================================

std::vector<float> VulkanTestShared::ReadbackHdrOutput(
	const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& pd,
	vk::Queue queue,
	uint32_t qfi,
	AttachmentManager& am,
	uint32_t renderWidth,
	uint32_t renderHeight)
{
	const vk::DeviceSize imageByteSize = renderWidth * renderHeight * 8; // RGBA16F = 8 B/px

	// Staging buffer: host-visible, TRANSFER_DST
	VulkanBuffer stagingBuf(device, pd, queue, qfi,
	                        imageByteSize,
	                        vk::BufferUsageFlagBits::eTransferDst,
	                        vk::MemoryPropertyFlagBits::eHostVisible |
	                            vk::MemoryPropertyFlagBits::eHostCoherent);

	// Transition HDR output: GENERAL → TRANSFER_SRC_OPTIMAL
	{
		// Transient command pool for one-shot operations
		vk::raii::CommandPool tempPool(device,
			vk::CommandPoolCreateInfo(
				vk::CommandPoolCreateFlagBits::eTransient, qfi));
		vk::raii::CommandBuffers cmdBufs(device,
			vk::CommandBufferAllocateInfo(*tempPool,
				vk::CommandBufferLevel::ePrimary, 1));
		auto& cmd = cmdBufs[0];
		cmd.begin(vk::CommandBufferBeginInfo(
			vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		auto& hdrColor = am.GetAttachment(AttachmentName::HDRColor);

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
			vk::Extent3D(renderWidth, renderHeight, 1));

		cmd.copyImageToBuffer(*hdrColor.ImageHandle(),
		                      vk::ImageLayout::eTransferSrcOptimal,
		                      stagingBuf.buffer(),
		                      {copyRegion});

		cmd.end();
		vk::SubmitInfo submitInfo({}, {}, {}, 1, &(*cmd));
		queue.submit(submitInfo, nullptr);
		device.waitIdle();
	}

	// Map, convert half-float → float
	const uint32_t pixelCount = renderWidth * renderHeight;
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
