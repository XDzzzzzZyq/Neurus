# src/render/passes/CMakeLists.txt - Render Pass Library

add_library(neurus_render_passes STATIC
	AttachmentManager.h
	AttachmentManager.cpp
	GeometryPass.h
	GeometryPass.cpp
	LightingPass.h
	LightingPass.cpp
	RenderPassManager.h
	RenderPassManager.cpp
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
