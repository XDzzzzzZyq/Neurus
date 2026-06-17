# src/CMakeLists.txt - Main Neurus library and executable

# ---------------------------------------------------------------------------
# Layer Libraries
# ---------------------------------------------------------------------------

# Core Types (header-only, no dependencies)
add_subdirectory(core)

# Data & Resource Layer (built first - no internal dependencies)
add_subdirectory(data)

# Renderer Layer (depends on: data)
add_subdirectory(render)

# Editor Layer (depends on: nothing from src/)
add_subdirectory(editor)

# UI Layer (depends on: editor for EventBus)
add_subdirectory(ui)

# Scene Layer (base types for scene graph - no internal deps)
add_subdirectory(scene)

# Application Layer (depends on: render, editor, ui, data, scene)
add_subdirectory(app)

# ---------------------------------------------------------------------------
# Main Executable
# ---------------------------------------------------------------------------

add_executable(Neurus
	main.cpp
)

target_link_libraries(Neurus PRIVATE
	neurus_app
	neurus_render
	neurus_editor
	neurus_ui
	neurus_data
	neurus_scene
	shader_headers
	ads::qtadvanceddocking-qt6
	Qt6::Core
	Qt6::Quick
	Qt6::Qml
	Qt6::Widgets
)

target_include_directories(Neurus PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/generated/shaders
)

# ADS_STATIC: tell Qt-Advanced-Docking-System headers to use static linkage
target_compile_definitions(Neurus PRIVATE ADS_STATIC)

# Windows: mark as GUI application (no console window in Release)
if(WIN32)
	set_target_properties(Neurus PROPERTIES
		WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
		VS_DEBUGGER_COMMAND "$(OutDir)Neurus.exe"
		VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)"
	)

	# Deploy Qt DLLs via windeployqt (handles Core, Gui, Qml, Quick and their dependencies)
	add_custom_command(TARGET Neurus POST_BUILD
		COMMAND "${WINDEPLOYQT}"
			--no-opengl-sw
			--no-system-d3d-compiler
			--no-compiler-runtime
			--verbose 0
			$<TARGET_FILE_DIR:Neurus>
		COMMENT "Deploying Qt DLLs via windeployqt"
	)

	# Copy res/ folder to the output directory so resources resolve at runtime
	# without walking up the source tree.
	add_custom_command(TARGET Neurus POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_SOURCE_DIR}/res"
			"$<TARGET_FILE_DIR:Neurus>/res"
		COMMENT "Copying res/ to output directory"
	)
endif()
