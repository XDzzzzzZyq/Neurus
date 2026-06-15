# src/editor/CMakeLists.txt — Editor Layer

add_library(neurus_editor STATIC
	EventBus.h
	EventBus.cpp
	EditorContext.h
	EditorContext.cpp
)

target_link_libraries(neurus_editor PUBLIC
	Qt6::Core
)

target_include_directories(neurus_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
