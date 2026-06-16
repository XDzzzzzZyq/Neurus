# src/editor/CMakeLists.txt - Editor Layer

add_library(neurus_editor STATIC
	EditorContext.h
	EditorContext.cpp
	events/UIEvents.h
	events/UIEvents.cpp
)

target_link_libraries(neurus_editor PUBLIC
	Qt6::Core
)

target_include_directories(neurus_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Enable Qt MOC for QObject subclasses (UIEvents, EditorContext)
set_target_properties(neurus_editor PROPERTIES AUTOMOC ON)
