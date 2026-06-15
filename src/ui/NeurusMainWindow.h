#pragma once

#include <QMainWindow>

class QDockWidget;  // forward decl for QWidget param

namespace ads {
class CDockManager;
class CDockWidget;
}

namespace neurus {

class NeurusMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit NeurusMainWindow(QWidget* parent = nullptr);
	~NeurusMainWindow() override;

	/**
	 * @brief Creates a dockable Viewport dock widget containing the given widget.
	 * @param viewportWidget The QWidget (from createWindowContainer) to embed.
	 * @return The created CDockWidget (owned by the dock manager).
	 */
	ads::CDockWidget* createViewportDock(QWidget* viewportWidget);

private:
	void CreateMenus();
	void CreateDocks();
	void SaveLayout();
	void RestoreDefaultLayout();

	ads::CDockManager* m_dockManager = nullptr;
	ads::CDockWidget* m_viewportDock = nullptr;  // created first in CreateDocks (central widget)
	bool m_viewportCreated = false;
};

} // namespace neurus
