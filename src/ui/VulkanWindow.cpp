#include "VulkanWindow.h"
#include "render/QVulkanRenderer.h"

VulkanWindow::VulkanWindow(QVulkanInstance* vulkanInstance,
                           const uint32_t* vertSpv, size_t vertSize,
                           const uint32_t* fragSpv, size_t fragSize)
	: m_vulkanInstance(vulkanInstance)
	, m_vertSpv(vertSpv), m_vertSize(vertSize)
	, m_fragSpv(fragSpv), m_fragSize(fragSize)
{
	setVulkanInstance(m_vulkanInstance);
}

QVulkanWindowRenderer* VulkanWindow::createRenderer()
{
	return new neurus::QVulkanRenderer(this, m_vertSpv, m_vertSize, m_fragSpv, m_fragSize);
}
