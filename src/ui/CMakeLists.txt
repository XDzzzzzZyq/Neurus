# src/ui/CMakeLists.txt - UI Layer (Qt6 QML)

add_library(neurus_ui STATIC
	UIManager.h
	UIManager.cpp
	panels/UIPanel.h
	panels/Outliner.h
	panels/Outliner.cpp
	panels/PropertyEditor.h
	panels/PropertyEditor.cpp
	panels/RenderConfigPanel.h
	panels/RenderConfigPanel.cpp
	panels/Viewport.h
	panels/Viewport.cpp
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

target_include_directories(neurus_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

# Qt resource file
qt_add_resources(neurus_ui "resources"
	PREFIX "/"
	FILES
		qml/main.qml
)

# Enable Qt MOC, UIC, RCC on this target
set_target_properties(neurus_ui PROPERTIES
	AUTOMOC ON
	AUTORCC ON
)
