#pragma once

#include <QMainWindow>
#include <Windows.h>

class QDockWidget;  // forward decl for QWidget param

namespace ads {
class CDockManager;
class CDockWidget;
}

namespace neurus {

class Viewport;
class OutlinerPanel;

class NeurusMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit NeurusMainWindow(QWidget* parent = nullptr);
	~NeurusMainWindow() override;

	/** @brief Returns the Viewport's native HWND for VkSurface creation. */
	HWND getViewportHwnd() const;

	/** @brief Returns viewport widget width in pixels. */
	int getViewportWidth() const;

	/** @brief Returns viewport widget height in pixels. */
	int getViewportHeight() const;

	/** @brief Returns non-owning raw pointer to the Viewport (for signal connections). */
	Viewport* getViewport() const;

    /** @brief Returns the viewport dock widget (for layout / restoreState). */
    ads::CDockWidget* getViewportDock() const { return win_viewportDock; }

    /** @brief Returns the OutlinerPanel for signal wiring. */
    class OutlinerPanel* getOutlinerPanel() const { return win_outlinerPanel; }

    /** @brief Returns the PropertyEditor for signal wiring. */
    class PropertyEditor* getPropertyEditor() const { return win_propertyEditor; }

private:
	void CreateMenus();
	void CreateDocks();
	void SaveLayout();
	void LoadLayout();
	void RestoreDefaultLayout();

    ads::CDockManager* win_dockManager = nullptr;
    ads::CDockWidget*  win_viewportDock = nullptr;
    Viewport*          win_viewportWidget = nullptr;
    OutlinerPanel*     win_outlinerPanel = nullptr;
    class PropertyEditor* win_propertyEditor = nullptr;  // Non-owning — Qt parent-child handles cleanup
};

} // namespace neurus
