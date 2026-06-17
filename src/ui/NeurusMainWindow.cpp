#include "NeurusMainWindow.h"

#include "editor/events/UIEvents.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QVBoxLayout>

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

namespace neurus {

NeurusMainWindow::NeurusMainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	setWindowTitle("Neurus");
	resize(1600, 900);

	// Disable opaque splitter resize for better Vulkan window container behavior
	ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, false);
	ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);

	m_dockManager = new ads::CDockManager(this);

	CreateDocks();
	LoadLayout();   // Restore saved layout if available
	CreateMenus();
}

NeurusMainWindow::~NeurusMainWindow() = default;

void NeurusMainWindow::setViewportWidget(QWidget* viewportWidget)
{
	if (viewportWidget != nullptr)
		m_viewportWidget = viewportWidget;
	m_viewportDock->setWidget(m_viewportWidget, ads::CDockWidget::ForceNoScrollArea);
}

void NeurusMainWindow::CreateMenus()
{
	auto* fileMenu = menuBar()->addMenu("&File");
	auto* exitAction = fileMenu->addAction("E&xit");
	exitAction->setShortcut(QKeySequence::Quit);
	connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

	auto* viewMenu = menuBar()->addMenu("&View");

	auto* saveLayoutAction = viewMenu->addAction("&Save Layout");
	saveLayoutAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
	connect(saveLayoutAction, &QAction::triggered, this, &NeurusMainWindow::SaveLayout);

	auto* resetLayoutAction = viewMenu->addAction("Restore &Default Layout");
	connect(resetLayoutAction, &QAction::triggered, this, &NeurusMainWindow::RestoreDefaultLayout);

	auto* toolsMenu = menuBar()->addMenu("&Tools");

	auto* screenshotAction = toolsMenu->addAction("Take &Screenshot");
	screenshotAction->setShortcut(QKeySequence("F12"));
	connect(screenshotAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestScreenshot();
	});

	auto* helpMenu = menuBar()->addMenu("&Help");
	auto* aboutAction = helpMenu->addAction("&About Neurus");
	connect(aboutAction, &QAction::triggered, this, [this]() {
		QMessageBox::about(this, "About Neurus",
			"<h2>Neurus</h2>"
			"<p>A C++20 Vulkan-HPP 1.4 real-time renderer.</p>"
			"<p>Version 0.1.0 (Triangle MVP)</p>");
	});
}

// Helper: create a labeled placeholder widget
static QWidget* makePlaceholder(const QString& text)
{
	auto* widget = new QWidget();
	auto* layout = new QVBoxLayout(widget);
	auto* label = new QLabel(text, widget);
	label->setAlignment(Qt::AlignCenter);
	QFont font = label->font();
	font.setPointSize(14);
	label->setFont(font);
	layout->addWidget(label);
	return widget;
}

void NeurusMainWindow::CreateDocks()
{
	if (m_viewportWidget == nullptr) {
		// --- Viewport (MUST be created FIRST - ADS central widget requirement) ---
		m_viewportDock = new ads::CDockWidget(m_dockManager, "Viewport");
		m_viewportDock->setWidget(makePlaceholder("Viewport"));  // for restoreState matching
		m_viewportDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
		// Use CenterDockWidgetArea instead of setCentralWidget so it stays dockable
		m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_viewportDock);
	}

	// --- Left: Shader Editor ---
	auto* shaderDock = new ads::CDockWidget(m_dockManager, "Shader Editor");
	shaderDock->setWidget(makePlaceholder("Shader Editor"));
	shaderDock->resize(280, 300);
	shaderDock->setMinimumSize(200, 200);
	m_dockManager->addDockWidget(ads::LeftDockWidgetArea, shaderDock);

	// --- Right: Outliner ---
	auto* outlinerDock = new ads::CDockWidget(m_dockManager, "Outliner");
	outlinerDock->setWidget(makePlaceholder("Outliner"));
	outlinerDock->resize(280, 300);
	outlinerDock->setMinimumSize(200, 200);
	m_dockManager->addDockWidget(ads::RightDockWidgetArea, outlinerDock);

	// --- Right: Property Editor ---
	auto* propDock = new ads::CDockWidget(m_dockManager, "Property Editor");
	propDock->setWidget(makePlaceholder("Property Editor"));
	propDock->resize(280, 300);
	propDock->setMinimumSize(200, 200);
	m_dockManager->addDockWidget(ads::RightDockWidgetArea, propDock, outlinerDock->dockAreaWidget());

	// --- Right: Render Config ---
	auto* configDock = new ads::CDockWidget(m_dockManager, "Render Config");
	configDock->setWidget(makePlaceholder("Render Config"));
	configDock->resize(280, 300);
	configDock->setMinimumSize(200, 200);
	m_dockManager->addDockWidget(ads::RightDockWidgetArea, configDock, outlinerDock->dockAreaWidget());

	// --- Bottom: Texture Viewer ---
	auto* textureDock = new ads::CDockWidget(m_dockManager, "Texture Viewer");
	textureDock->setWidget(makePlaceholder("Texture Viewer"));
	textureDock->resize(300, 200);
	textureDock->setMinimumSize(200, 150);
	m_dockManager->addDockWidget(ads::BottomDockWidgetArea, textureDock);
}

void NeurusMainWindow::SaveLayout()
{
	QString path = QApplication::applicationDirPath() + "/layout.ads";
	QFile file(path);
	if (file.open(QIODevice::WriteOnly))
	{
		QByteArray state = m_dockManager->saveState();
		file.write(state);
		file.close();
	}
}

void NeurusMainWindow::LoadLayout()
{
	QString path = QApplication::applicationDirPath() + "/layout.ads";
	QFile file(path);
	if (file.open(QIODevice::ReadOnly))
	{
		QByteArray state = file.readAll();
		file.close();
		m_dockManager->restoreState(state);
	}
}

void NeurusMainWindow::RestoreDefaultLayout()
{
	// Delete all non-viewport docks
	auto docks = m_dockManager->dockWidgetsMap();
	for (auto it = docks.begin(); it != docks.end(); ++it)
	{
		if (it.value() != m_viewportDock)
		{
			it.value()->deleteDockWidget();
		}
	}

	// Re-create the default dock arrangement
	CreateDocks();
}

} // namespace neurus
