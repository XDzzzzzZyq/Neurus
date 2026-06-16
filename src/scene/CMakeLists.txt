# src/scene/CMakeLists.txt — Scene Layer Library
#
# Contains scene graph base classes: UID, ObjectID, and eventually
# Camera, Light, Mesh, and other scene objects.
#
# Dependencies: glm (header-only math), Vulkan (via Mesh→Material include chain)
# Consumed by: editor, render, test

	add_library(neurus_scene STATIC
	Camera.h
	Camera.cpp
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

target_link_libraries(neurus_scene PUBLIC
	glm
)

target_include_directories(neurus_scene PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(neurus_scene PUBLIC glm Vulkan::Vulkan)

target_compile_features(neurus_scene PUBLIC cxx_std_20)

# Scene objects reference Material (render) and MeshData (data) types.
# glm is required by Transform3D for matrix/vector math.
target_link_libraries(neurus_scene PUBLIC
	glm
	Vulkan::Vulkan
)
