#pragma once

#include <QMainWindow>

namespace neurus {

/**
 * @brief Main application window with dockable editor panels.
 *
 * Provides a QMainWindow with a central viewport and five dockable
 * tool panels arranged in left, right, and bottom dock areas.
 * All panels are labeled placeholders awaiting future implementation.
 */
class NeurusMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs the main window with default dock layout.
	 * @param parent Optional parent widget.
	 */
	explicit NeurusMainWindow(QWidget* parent = nullptr);

	/** @brief Default destructor. Qt parent-child ownership handles cleanup. */
	~NeurusMainWindow() override;

private:
	/** @brief Creates the menu bar with File, View, and Help menus. */
	void CreateMenus();

	/** @brief Creates the central viewport placeholder widget. */
	void CreateCentralWidget();

	/** @brief Creates and arranges all dock widgets. */
	void CreateDocks();
};

} // namespace neurus
