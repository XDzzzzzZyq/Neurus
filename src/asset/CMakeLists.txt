# src/asset/CMakeLists.txt - Asset Layer (CPU-side data only)

add_library(neurus_asset STATIC
	ImageData.h
	ImageData.cpp
	MeshData.h
	MeshData.cpp
)

target_link_libraries(neurus_asset PUBLIC
	Vulkan::Vulkan
	glm
	stb
	neurus_core
)

target_include_directories(neurus_asset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(neurus_asset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
