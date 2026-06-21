# src/editor/CMakeLists.txt - Editor Layer

add_library(neurus_editor STATIC
	Editor.h
	Editor.cpp
	Input.h
	Input.cpp
	Context.h
	Context.cpp
	SelectionManager.h
	SelectionController.h
	SelectionController.cpp
	controllers/Controllers.h
	controllers/CameraController.h
	controllers/CameraController.cpp
	events/UIEvents.h
	events/UIEvents.cpp
)

target_link_libraries(neurus_editor PUBLIC
	Qt6::Core
	neurus_scene
	stb
)

target_include_directories(neurus_editor PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(neurus_editor PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Enable Qt MOC for QObject subclasses (UIEvents, EditorContext)
set_target_properties(neurus_editor PROPERTIES AUTOMOC ON)
