# src/app/CMakeLists.txt - Application layer (lifecycle management)

add_library(neurus_app STATIC
	Application.h
	Application.cpp
)

target_include_directories(neurus_app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${CMAKE_BINARY_DIR}/generated/shaders
)

target_link_libraries(neurus_app PRIVATE
	neurus_render
	neurus_editor
	neurus_ui
	neurus_data
	neurus_scene
)
