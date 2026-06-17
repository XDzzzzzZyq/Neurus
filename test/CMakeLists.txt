# test/CMakeLists.txt - Unit Tests

add_executable(neurus_test
	test_main.cpp
	render/test_buffers.cpp
	render/test_vulkan_buffer.cpp
	render/test_vulkan_context.cpp
	render/test_shader_program.cpp
	render/test_meshdata.cpp
	render/test_shader_module.cpp
	render/test_swapchain.cpp
	render/test_syncobjects.cpp
	render/test_vulkanimage.cpp
	render/test_descriptor.cpp
	render/test_compute_pipeline.cpp
	render/test_texture.cpp
	render/test_material.cpp
	render/test_pipeline.cpp
	render/test_attachments.cpp
	render/test_commandbuffer.cpp
	render/test_renderpass.cpp
	render/test_gbuffer.cpp
	render/test_lighting.cpp
	render/test_model_render.cpp
	render/test_screenshot.cpp
	render/test_scene_wiring.cpp
	data/test_gpu_resource_cache.cpp
	editor/test_event_bus.cpp
	editor/test_event_bus_expanded.cpp
	editor/test_event_bus_typed.cpp
	editor/test_editor_context.cpp
	editor/test_scene_status.cpp
	scene/test_camera.cpp
	scene/test_mesh.cpp
	scene/test_uid.cpp
	scene/test_transform.cpp
	scene/test_light.cpp
	scene/test_sprite.cpp
	scene/test_scene.cpp
	scene/test_scene_integration.cpp
	scene/test_scene_const_access.cpp
	scene/test_debug.cpp
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
