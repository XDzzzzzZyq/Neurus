# src/render/CMakeLists.txt - Renderer Layer

add_library(neurus_render STATIC
	buffers/Buffer.h
	buffers/Buffer.cpp
	buffers/BufferLayout.h
	buffers/BufferLayout.cpp
	buffers/GPUBuffer.h
	buffers/GPUBuffer.cpp
	buffers/IndexBuffer.h
	buffers/IndexBuffer.cpp
	buffers/StagingBuffer.h
	buffers/StagingBuffer.cpp
	buffers/UniformBuffer.h
	buffers/VertexBuffer.h
	buffers/VertexBuffer.cpp
	Barrier.h
	Barrier.cpp
	Image.h
	Image.cpp
	DescriptorManager.h
	DescriptorManager.cpp
	ComputePipelineBuilder.h
	ComputePipelineBuilder.cpp
	CommandBuffer.h
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
	PipelineBuilder.h
	PipelineBuilder.cpp
	RenderCache.h
	RenderCache.cpp
	RenderContext.h
	DeferredRenderer.h
	DeferredRenderer.cpp
	Screenshot.h
	Screenshot.cpp
)

add_subdirectory(passes)

target_link_libraries(neurus_render PUBLIC
	Vulkan::Vulkan
	Qt6::Gui
	glm
	stb
	neurus_core
	neurus_scene
	neurus_render_passes
)

target_include_directories(neurus_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(neurus_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(neurus_render PRIVATE ${CMAKE_BINARY_DIR}/generated/shaders)

# Ensure generated shader headers exist before compiling DeferredRenderer.cpp
add_dependencies(neurus_render shader_headers)
