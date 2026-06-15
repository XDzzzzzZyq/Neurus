# src/scene/CMakeLists.txt — Scene Layer Library
#
# Contains scene graph base classes: UID, ObjectID, and eventually
# Camera, Light, Mesh, and other scene objects.
#
# Dependencies: none (pure C++20, no Vulkan/Qt)
# Consumed by: editor, render, test

add_library(neurus_scene STATIC
	UID.h
	UID.cpp
	Transform.h
)

target_include_directories(neurus_scene PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(neurus_scene PUBLIC cxx_std_20)
