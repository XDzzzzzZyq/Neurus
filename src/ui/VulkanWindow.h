#pragma once

#include <QVulkanWindow>
#include <cstdint>

namespace neurus { class QVulkanRenderer; }

class VulkanWindow : public QVulkanWindow
{
	Q_OBJECT

public:
	VulkanWindow(QVulkanInstance* vulkanInstance,
	             const uint32_t* vertSpv, size_t vertSize,
	             const uint32_t* fragSpv, size_t fragSize);

protected:
	QVulkanWindowRenderer* createRenderer() override;

private:
	QVulkanInstance* m_vulkanInstance;
	const uint32_t* m_vertSpv;
	size_t m_vertSize;
	const uint32_t* m_fragSpv;
	size_t m_fragSize;
};
