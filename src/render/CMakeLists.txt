# src/render/CMakeLists.txt — Renderer Layer

add_library(neurus_render STATIC
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
	Renderer.h
	Renderer.cpp
	QVulkanRenderer.h
	QVulkanRenderer.cpp
)

target_link_libraries(neurus_render PUBLIC
	Vulkan::Vulkan
	Qt6::Gui
	glm
	neurus_editor
)

target_include_directories(neurus_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
