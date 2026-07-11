/**
 * @file LightingGPU.cpp
 * @brief Light SSBO storage implementation.
 */

#include "render/resources/LightingGPU.h"

#include "render/buffers/GPUBuffer.h"
#include "Log.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LightingGPU::LightingGPU(const vk::raii::Device& device,
                         const vk::raii::PhysicalDevice& physicalDevice)
	: m_device(device)
	, m_physicalDevice(physicalDevice)
	, m_graphicsQueue(nullptr)
	, m_queueFamilyIndex(0)
{
	NEURUS_LOG("[LightingGPU] Created");
}

void LightingGPU::Init(vk::Queue graphicsQueue, uint32_t queueFamilyIndex)
{
	m_graphicsQueue = graphicsQueue;
	m_queueFamilyIndex = queueFamilyIndex;
	NEURUS_LOG("[LightingGPU] Initialized (qfi=" << queueFamilyIndex << ")");
}

// ---------------------------------------------------------------------------
// Point light SSBO
// ---------------------------------------------------------------------------

void LightingGPU::UpdatePointLights(const std::vector<PointLightStruct>& lights)
{
	const uint32_t newCount = static_cast<uint32_t>(lights.size());
	m_pointLightCount = newCount;

	if (newCount == 0)
	{
		m_pointLightSSBO.reset();
		NEURUS_LOG("[LightingGPU] No point lights - SSBO released");
		return;
	}

	const vk::DeviceSize bufferSize = newCount * sizeof(PointLightStruct);

	m_pointLightSSBO = std::make_unique<GPUBuffer>(
		m_device, m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex,
		bufferSize,
		vk::BufferUsageFlagBits::eStorageBuffer,
		"PointLightSSBO");
	m_pointLightSSBO->Upload(lights.data(), bufferSize);

	NEURUS_LOG("[LightingGPU] Uploaded " << newCount << " point lights"
	           << " (" << bufferSize << " bytes)");
}

const GPUBuffer* LightingGPU::GetPointLightSSBO() const
{
	return m_pointLightSSBO ? m_pointLightSSBO.get() : nullptr;
}

uint32_t LightingGPU::GetPointLightCount() const
{
	return m_pointLightCount;
}

// ---------------------------------------------------------------------------
// Sun light SSBO
// ---------------------------------------------------------------------------

void LightingGPU::UpdateSunLights(const std::vector<SunLightStruct>& lights)
{
	const uint32_t newCount = static_cast<uint32_t>(lights.size());
	m_sunLightCount = newCount;

	if (newCount == 0)
	{
		m_sunLightSSBO.reset();
		NEURUS_LOG("[LightingGPU] No sun lights - SSBO released");
		return;
	}

	const vk::DeviceSize bufferSize = newCount * sizeof(SunLightStruct);

	m_sunLightSSBO = std::make_unique<GPUBuffer>(
		m_device, m_physicalDevice, m_graphicsQueue, m_queueFamilyIndex,
		bufferSize,
		vk::BufferUsageFlagBits::eStorageBuffer,
		"SunLightSSBO");
	m_sunLightSSBO->Upload(lights.data(), bufferSize);

	NEURUS_LOG("[LightingGPU] Uploaded " << newCount << " sun lights"
	           << " (" << bufferSize << " bytes)");
}

const GPUBuffer* LightingGPU::GetSunLightSSBO() const
{
	return m_sunLightSSBO ? m_sunLightSSBO.get() : nullptr;
}

uint32_t LightingGPU::GetSunLightCount() const
{
	return m_sunLightCount;
}

} // namespace neurus
