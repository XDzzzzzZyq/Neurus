#include "QVulkanRenderer.h"
#include <array>

namespace neurus {

QVulkanRenderer::QVulkanRenderer(QVulkanWindow* window,
                                 const uint32_t* vertSpv, size_t vertSize,
                                 const uint32_t* fragSpv, size_t fragSize)
    : m_window(window)
    , m_vertSpv(vertSpv)
    , m_vertSize(vertSize)
    , m_fragSpv(fragSpv)
    , m_fragSize(fragSize)
{
}

void QVulkanRenderer::preInitResources()
{
    // Optional: nothing needed for triangle MVP
}

void QVulkanRenderer::initResources()
{
    VkDevice dev = m_window->device();

    // --- Create vertex shader module ---
    VkShaderModuleCreateInfo vertInfo{};
    vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertInfo.codeSize = m_vertSize;
    vertInfo.pCode = m_vertSpv;
    if (vkCreateShaderModule(dev, &vertInfo, nullptr, &m_vertModule) != VK_SUCCESS)
        qFatal("Failed to create vertex shader module");

    // --- Create fragment shader module ---
    VkShaderModuleCreateInfo fragInfo{};
    fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragInfo.codeSize = m_fragSize;
    fragInfo.pCode = m_fragSpv;
    if (vkCreateShaderModule(dev, &fragInfo, nullptr, &m_fragModule) != VK_SUCCESS)
        qFatal("Failed to create fragment shader module");

    // --- Shader stages ---
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = m_vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = m_fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    // --- Vertex input (no vertex buffers - triangle MVP) ---
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // --- Input assembly ---
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // --- Viewport + scissor (dynamic) ---
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // --- Rasterizer ---
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    // --- Multisampling ---
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // --- Color blend ---
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    // --- Dynamic states ---
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // --- Pipeline layout (empty) ---
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        qFatal("Failed to create pipeline layout");

    // --- Graphics pipeline (using QVulkanWindow's default render pass) ---
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_window->defaultRenderPass();  // QVulkanWindow's render pass
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline");
}

void QVulkanRenderer::initSwapChainResources()
{
    // Nothing to do - viewport/scissor set dynamically in startNextFrame
}

void QVulkanRenderer::releaseSwapChainResources()
{
    // Nothing to release
}

void QVulkanRenderer::releaseResources()
{
    VkDevice dev = m_window->device();

    if (m_pipeline)      { vkDestroyPipeline(dev, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    if (m_pipelineLayout){ vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
    if (m_vertModule)    { vkDestroyShaderModule(dev, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
    if (m_fragModule)    { vkDestroyShaderModule(dev, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
}

void QVulkanRenderer::startNextFrame()
{
    VkCommandBuffer cb = m_window->currentCommandBuffer();
    VkFramebuffer fb = m_window->currentFramebuffer();
    const QSize sz = m_window->swapChainImageSize();

    // If window is minimized, skip
    if (sz.isEmpty())
    {
        m_window->frameReady();
        m_window->requestUpdate();
        return;
    }

    // --- Begin render pass ---
    VkClearValue clearValue;
    clearValue.color = { { 0.1f, 0.1f, 0.15f, 1.0f } };  // dark blue-grey

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_window->defaultRenderPass();
    rpBegin.framebuffer = fb;
    rpBegin.renderArea.extent = { static_cast<uint32_t>(sz.width()), static_cast<uint32_t>(sz.height()) };
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cb, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // --- Dynamic viewport + scissor ---
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(sz.width());
    viewport.height = static_cast<float>(sz.height());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = { static_cast<uint32_t>(sz.width()), static_cast<uint32_t>(sz.height()) };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    // --- Bind pipeline and draw ---
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdDraw(cb, 3, 1, 0, 0);  // 3 vertices, 1 instance

    vkCmdEndRenderPass(cb);

    m_window->frameReady();
    m_window->requestUpdate();  // Request next frame (continuous rendering)
}

} // namespace neurus
