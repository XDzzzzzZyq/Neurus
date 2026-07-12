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

private:
	void CreateMenus();
	void CreateDocks();
	void SaveLayout();
	void LoadLayout();
	void RestoreDefaultLayout();

    ads::CDockManager* win_dockManager = nullptr;
    ads::CDockWidget*  win_viewportDock = nullptr;
    Viewport*          win_viewportWidget = nullptr;  // Non-owning — Qt parent-child handles cleanup
};

} // namespace neurus
