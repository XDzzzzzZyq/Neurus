# src/core/CMakeLists.txt - Core Type Definitions (header-only)

add_library(neurus_core INTERFACE)

target_include_directories(neurus_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(neurus_core INTERFACE
	glm
)
