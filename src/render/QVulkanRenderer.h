#pragma once

// Include vulkan.h BEFORE QVulkanWindow because Qt's qvulkaninstance.h
// defines VK_NO_PROTOTYPES which hides C API function prototypes.
// By including vulkan.h first, the prototypes are available and Qt's
// subsequent include is a no-op (header guard).
#include <vulkan/vulkan.h>
#include <QVulkanWindow>

namespace neurus {

class QVulkanRenderer : public QVulkanWindowRenderer
{
public:
    QVulkanRenderer(QVulkanWindow* window,
                    const uint32_t* vertSpv, size_t vertSize,
                    const uint32_t* fragSpv, size_t fragSize);

    // QVulkanWindowRenderer interface
    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;

private:
    QVulkanWindow* m_window;
    
    // SPIR-V bytecode (stored for initResources)
    const uint32_t* m_vertSpv;
    size_t m_vertSize;
    const uint32_t* m_fragSpv;
    size_t m_fragSize;

    // Vulkan resources (C API handles - QVulkanWindow uses C API internally)
    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace neurus
