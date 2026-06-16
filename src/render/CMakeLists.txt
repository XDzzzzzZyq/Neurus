# src/render/CMakeLists.txt — Renderer Layer

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
	DescriptorManager.h
	DescriptorManager.cpp
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
	Renderer.h
	Renderer.cpp
	Material.h
	Material.cpp
	QVulkanRenderer.h
	QVulkanRenderer.cpp
)

target_link_libraries(neurus_render PUBLIC
	Vulkan::Vulkan
	Qt6::Gui
	glm
	stb
	neurus_editor
)

target_include_directories(neurus_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
