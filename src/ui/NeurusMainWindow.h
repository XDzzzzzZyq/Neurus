#pragma once

#include <QMainWindow>
#include <Windows.h>

class QDockWidget;  // forward decl for QWidget param

namespace ads {
class CDockManager;
class CDockWidget;
}

namespace neurus {

class Scene;
class PropertyEditor;
class VulkanWidget;

class NeurusMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit NeurusMainWindow(QWidget* parent = nullptr);
	~NeurusMainWindow() override;

	/** @brief Returns the VulkanWidget's native HWND for VkSurface creation. */
	HWND getViewportHwnd() const;

	/** @brief Returns viewport widget width in pixels. */
	int getViewportWidth() const;

	/** @brief Returns viewport widget height in pixels. */
	int getViewportHeight() const;

	/** @brief Returns non-owning raw pointer to the VulkanWidget (for signal connections). */
	VulkanWidget* getVulkanWidget() const;

    /** @brief Returns the viewport dock widget (for layout / restoreState). */
    ads::CDockWidget* getViewportDock() const { return m_viewportDock; }

    /**
     * @brief Sets the active scene on the property editor.
     * @param scene Non-owning pointer to the Scene.
     */
    void SetScene(Scene* scene);

private:
	void CreateMenus();
	void CreateDocks();
	void SaveLayout();
	void LoadLayout();
	void RestoreDefaultLayout();

    ads::CDockManager* m_dockManager = nullptr;
    ads::CDockWidget*  m_viewportDock = nullptr;
    VulkanWidget*      m_viewportWidget = nullptr;  // Non-owning — Qt parent-child handles cleanup
    PropertyEditor*    m_propertyEditor = nullptr;  // Non-owning — Qt parent-child handles cleanup
};

} // namespace neurus
