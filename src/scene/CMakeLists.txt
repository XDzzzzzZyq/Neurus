# src/scene/CMakeLists.txt - Scene Layer Library
#
# Contains scene graph base classes: UID, ObjectID, and eventually
# Camera, Light, Mesh, and other scene objects.
#
# Dependencies: glm (header-only math), Vulkan (via Mesh→Material include chain)
# Consumed by: editor, render, test

	add_library(neurus_scene STATIC
	Camera.h
	Camera.cpp
	DefaultScene.h
	DefaultScene.cpp
	Mesh.h
	Mesh.cpp
	UID.h
	UID.cpp
	Transform.h
	Light.h
	Light.cpp
	Sprite.h
	Sprite.cpp
	Scene.h
	Scene.cpp
	DebugLine.h
	DebugLine.cpp
	DebugPoints.h
	DebugPoints.cpp
)

target_include_directories(neurus_scene PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(neurus_scene PUBLIC
	glm
)

target_link_libraries(neurus_scene PRIVATE
	Vulkan::Vulkan
)

target_compile_features(neurus_scene PUBLIC cxx_std_20)
