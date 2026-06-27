# src/render/passes/CMakeLists.txt - Render Pass Library

add_library(neurus_render_passes STATIC
	RenderCache.h
	RenderCache.cpp
	ComputePass.h
	GeometryPass.h
	GeometryPass.cpp
	IBLPass.h
	IBLPass.cpp
	LightingPass.h
	LightingPass.cpp
	Pass.h
	Pass.cpp
	RenderContext.h
	ShadowDepthPass.h
	ShadowDepthPass.cpp
	SSAOPass.h
	SSAOPass.cpp
	SyncObjects.h
)

target_link_libraries(neurus_render_passes PUBLIC
	Vulkan::Vulkan
	Qt6::Gui
	glm
	stb
	neurus_core
	neurus_scene
)

target_include_directories(neurus_render_passes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(neurus_render_passes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_include_directories(neurus_render_passes PRIVATE ${CMAKE_BINARY_DIR}/generated/shaders)
