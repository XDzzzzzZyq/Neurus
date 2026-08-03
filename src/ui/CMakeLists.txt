# src/ui/CMakeLists.txt - UI Layer (Qt6 QML)

add_library(neurus_ui STATIC
	UIManager.h
	UIManager.cpp
	UIContext.h
	UIContext.cpp
	Icons.h
	Icons.cpp
	items/ScalarSlider.h
	items/ScalarSlider.cpp
	items/OutlinerRow.h
	items/OutlinerRow.cpp
	items/Vec3Spin.h
	items/Vec3Spin.cpp
	items/ShaderFieldRow.h
	items/ShaderFieldRow.cpp
	items/ShaderStructModel.h
	items/ShaderStructModel.cpp
	items/ShaderFieldDelegate.h
	items/ShaderFieldDelegate.cpp
	elements/ShaderHighlighter.h
	elements/ShaderHighlighter.cpp
	elements/CodeEditor.h
	elements/CodeEditor.cpp
	presets/CameraProperties.h
	presets/CameraProperties.cpp
	presets/MeshProperties.h
	presets/MeshProperties.cpp
	presets/LightProperties.h
	presets/LightProperties.cpp
	presets/EnvironmentProperties.h
	presets/EnvironmentProperties.cpp
	panels/UIPanel.h
	panels/Outliner.h
	panels/Outliner.cpp
	panels/PropertyPanel.h
	panels/PropertyPanel.cpp
	panels/ProfilingPanel.h
	panels/ProfilingPanel.cpp
	panels/RenderConfigPanel.h
	panels/RenderConfigPanel.cpp
	panels/Viewport.h
	panels/Viewport.cpp
	panels/ShaderEditorPanel.h
	panels/ShaderEditorPanel.cpp
)

target_link_libraries(neurus_ui PUBLIC
	Qt6::Core
	Qt6::Quick
	Qt6::Qml
	Qt6::Widgets
	neurus_editor
	ads::qtadvanceddocking-qt6
)

target_link_libraries(neurus_ui PRIVATE
	Vulkan::Vulkan
)

# ADS_STATIC: tell Qt-Advanced-Docking-System headers to use static linkage
target_compile_definitions(neurus_ui PUBLIC ADS_STATIC)

target_include_directories(neurus_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

# Qt resource file (QML)
qt_add_resources(neurus_ui "resources"
	PREFIX "/"
	FILES
		qml/main.qml
		qml/outliner.qss
		qml/shader_editor.qss
)

# Qt resource file (SVG icons)
qt_add_resources(neurus_ui "icons"
	PREFIX "/icons"
	BASE "${CMAKE_SOURCE_DIR}/res/ui/icons"
	FILES
		"${CMAKE_SOURCE_DIR}/res/ui/icons/scene/camera.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/scene/environment.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/scene/light.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/scene/mesh.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/editor/movie.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/editor/movie_off.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/editor/preview_invisible.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/editor/preview_visible.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/editor/render_invisible.svg"
		"${CMAKE_SOURCE_DIR}/res/ui/icons/editor/render_visible.svg"
)

# Enable Qt MOC, UIC, RCC on this target
set_target_properties(neurus_ui PROPERTIES
	AUTOMOC ON
	AUTORCC ON
)
