/**
 * @file LightingGPU.cpp
 * @brief Light SSBO storage implementation using ArrayBuffer<T>.
 */

#include "render/resources/LightingGPU.h"

#include "Log.h"

#include <cstdint>
#include <vector>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LightingGPU::LightingGPU(const vk::raii::Device& device,
                         const vk::raii::PhysicalDevice& physicalDevice,
                         vk::Queue graphicsQueue,
                         uint32_t queueFamilyIndex)
	: m_pointLightSSBO(device, physicalDevice, graphicsQueue, queueFamilyIndex,
	                   vk::BufferUsageFlagBits::eStorageBuffer, "PointLightSSBO")
	, m_sunLightSSBO(device, physicalDevice, graphicsQueue, queueFamilyIndex,
	                 vk::BufferUsageFlagBits::eStorageBuffer, "SunLightSSBO")
{
	NEURUS_LOG("[LightingGPU] Created");
}

// ---------------------------------------------------------------------------
// Point light SSBO
// ---------------------------------------------------------------------------

void LightingGPU::UpdatePointLights(const std::vector<PointLightStruct>& lights)
{
	m_pointLightSSBO.Upload(lights);

	NEURUS_LOG("[LightingGPU] Uploaded " << m_pointLightSSBO.size() << " point lights");
}

const GPUBuffer* LightingGPU::GetPointLightSSBO() const
{
	return m_pointLightSSBO.gpuBuffer();
}

uint32_t LightingGPU::GetPointLightCount() const
{
	return m_pointLightSSBO.size();
}

// ---------------------------------------------------------------------------
// Sun light SSBO
// ---------------------------------------------------------------------------

void LightingGPU::UpdateSunLights(const std::vector<SunLightStruct>& lights)
{
	m_sunLightSSBO.Upload(lights);

	NEURUS_LOG("[LightingGPU] Uploaded " << m_sunLightSSBO.size() << " sun lights");
}

const GPUBuffer* LightingGPU::GetSunLightSSBO() const
{
	return m_sunLightSSBO.gpuBuffer();
}

uint32_t LightingGPU::GetSunLightCount() const
{
	return m_sunLightSSBO.size();
}

} // namespace neurus
