# src/CMakeLists.txt — Main Neurus library and executable

# ---------------------------------------------------------------------------
# Layer Libraries
# ---------------------------------------------------------------------------

# Data & Resource Layer (built first — no internal dependencies)
add_subdirectory(data)

# Renderer Layer (depends on: data)
add_subdirectory(render)

# Editor Layer (depends on: nothing from src/)
add_subdirectory(editor)

# UI Layer (depends on: editor for EventBus)
add_subdirectory(ui)

# ---------------------------------------------------------------------------
# Main Executable
# ---------------------------------------------------------------------------

add_executable(Neurus
	main.cpp
)

target_link_libraries(Neurus PRIVATE
	neurus_render
	neurus_editor
	neurus_ui
	neurus_data
	shader_headers
	Qt6::Core
	Qt6::Quick
	Qt6::Qml
)

target_include_directories(Neurus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/generated/shaders
)

# Windows: mark as GUI application (no console window in Release)
if(WIN32)
	set_target_properties(Neurus PROPERTIES
		WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
	)

	# Copy Vulkan SDK runtime DLL to output directory (ensures header version matches)
	add_custom_command(TARGET Neurus POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$ENV{VULKAN_SDK}/Bin/vulkan-1.dll"
			$<TARGET_FILE_DIR:Neurus>
		COMMENT "Copying vulkan-1.dll from SDK to ensure version match"
	)
endif()
