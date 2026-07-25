# src/asset/CMakeLists.txt - Asset Layer (CPU-side data + project persistence)

add_library(neurus_asset STATIC
	ConfigComponent.h
	ConfigComponent.cpp
	ImageData.h
	ImageData.cpp
	MeshData.h
	MeshData.cpp
	Project.h
	Project.cpp
	TypeRegistration.cpp
)

target_link_libraries(neurus_asset PUBLIC
	glm
	stb
	neurus_core
	neurus_scene
	cereal
)

target_include_directories(neurus_asset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(neurus_asset PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${Vulkan_INCLUDE_DIRS}
)

target_compile_features(neurus_asset PUBLIC cxx_std_20)
