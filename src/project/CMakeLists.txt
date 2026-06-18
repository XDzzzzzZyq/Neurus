# src/project/CMakeLists.txt - Project Layer Library
#
# High-level project logic using cereal serialization on scene types.
# Dependencies: neurus_scene, cereal (header-only serialization)
# Consumed by: Neurus executable

add_library(neurus_project STATIC
	Project.h
	Project.cpp
	TypeRegistration.cpp
)

target_include_directories(neurus_project PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(neurus_project PUBLIC
	neurus_scene
	glm
	cereal
)

target_compile_features(neurus_project PUBLIC cxx_std_20)
