# src/render/CMakeLists.txt — Renderer Layer

add_library(neurus_render STATIC
	BufferLayout.h
	BufferLayout.cpp
	IndexBuffer.h
	IndexBuffer.cpp
	VertexBuffer.h
	VertexBuffer.cpp
	VulkanBuffer.h
	VulkanBuffer.cpp
	VulkanImage.h
	VulkanImage.cpp
	VulkanContext.h
	VulkanContext.cpp
	Swapchain.h
	Swapchain.cpp
	ShaderProgram.h
	ShaderProgram.cpp
	ShaderModule.h
	ShaderModule.cpp
	ShaderLib.h
	ShaderLib.cpp
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
