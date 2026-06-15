# test/CMakeLists.txt — Unit Tests

add_executable(neurus_test
	test_main.cpp
	render/test_vulkan_context.cpp
	render/test_shader_program.cpp
	render/test_swapchain.cpp
	render/test_renderer.cpp
	editor/test_event_bus.cpp
	editor/test_event_bus_expanded.cpp
	editor/test_editor_context.cpp
	scene/test_uid.cpp
	scene/test_transform.cpp
	scene/test_scene.cpp
)

target_link_libraries(neurus_test PRIVATE
	neurus_scene
	neurus_render
	neurus_editor
	neurus_ui
	neurus_data
	shader_headers
	gtest
	Vulkan::Vulkan
	Qt6::Core
	Qt6::Test
	Qt6::Quick
	Qt6::Qml
)

target_include_directories(neurus_test PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/generated/shaders
)

# Enable Qt MOC for test files containing QObject subclasses
set_target_properties(neurus_test PROPERTIES AUTOMOC ON)

# MSVC: disable Qt's MOC warnings in test code
if(MSVC)
	target_compile_options(neurus_test PRIVATE /wd4100 /wd4189)
endif()

# Windows: deploy Qt DLLs so test executable can run standalone
# MUST come before gtest_discover_tests (POST_BUILD ordering)
if(WIN32 AND WINDEPLOYQT)
	# Qt6Test DLL is not auto-deployed by windeployqt, so we copy it manually
	file(REAL_PATH "${Qt6_DIR}/../../../bin" _qt6_bin_dir)
	add_custom_command(TARGET neurus_test POST_BUILD
		COMMAND "${WINDEPLOYQT}"
			--no-opengl-sw
			--no-system-d3d-compiler
			--no-compiler-runtime
			--verbose 0
			$<TARGET_FILE_DIR:neurus_test>
		COMMAND ${CMAKE_COMMAND} -E copy
			"${_qt6_bin_dir}/$<IF:$<CONFIG:Debug>,Qt6Testd.dll,Qt6Test.dll>"
			$<TARGET_FILE_DIR:neurus_test>
		COMMENT "Deploying Qt DLLs for test executable (including Qt6Test)"
	)
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(neurus_test
	TEST_PREFIX "Neurus."
	DISCOVERY_TIMEOUT 30
)
