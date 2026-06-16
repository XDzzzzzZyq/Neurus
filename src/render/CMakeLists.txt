# src/render/CMakeLists.txt - Renderer Layer

add_library(neurus_render STATIC
	buffers/BufferLayout.h
	buffers/BufferLayout.cpp
	buffers/IndexBuffer.h
	buffers/IndexBuffer.cpp
	buffers/VertexBuffer.h
	buffers/VertexBuffer.cpp
	VulkanBuffer.h
	VulkanBuffer.cpp
	VulkanImage.h
	VulkanImage.cpp
	AttachmentManager.h
	AttachmentManager.cpp
	DescriptorManager.h
	DescriptorManager.cpp
	ComputePipelineBuilder.h
	ComputePipelineBuilder.cpp
	CommandBuffer.h
	VulkanContext.h
	VulkanContext.cpp
	Swapchain.h
	Swapchain.cpp
	shaders/ShaderProgram.h
	shaders/ShaderProgram.cpp
	shaders/ShaderModule.h
	shaders/ShaderModule.cpp
	shaders/ShaderLib.h
	shaders/ShaderLib.cpp
	Texture.h
	Texture.cpp
	TextureLib.h
	TextureLib.cpp
	PipelineBuilder.h
	PipelineBuilder.cpp
	Material.h
	Material.cpp
	RenderPassManager.h
	RenderPassManager.cpp
	GeometryPass.h
	GeometryPass.cpp
	LightingPass.h
	LightingPass.cpp
	DeferredRenderer.h
	DeferredRenderer.cpp
	SyncObjects.h
	Screenshot.h
	Screenshot.cpp
)

target_link_libraries(neurus_render PUBLIC
	Vulkan::Vulkan
	Qt6::Gui
	glm
	stb
	neurus_editor
	neurus_core
)

target_include_directories(neurus_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
