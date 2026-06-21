#include "NeurusMainWindow.h"
#include "VulkanWidget.h"

#include "editor/events/UIEvents.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QVBoxLayout>

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

namespace neurus {

// =========================================================================
// Constructor / Destructor
// =========================================================================

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

	// Create VulkanWidget after docks — CreateDocks() places it as viewport content
	m_viewportWidget = new VulkanWidget();
	m_viewportWidget->resize(800, 600);
	m_viewportWidget->winId();  // Force native window handle creation
	m_viewportDock->setWidget(m_viewportWidget, ads::CDockWidget::ForceNoScrollArea);
}

NeurusMainWindow::~NeurusMainWindow() = default;

// =========================================================================
// Viewport accessors
// =========================================================================

HWND NeurusMainWindow::getViewportHwnd() const
{
	return m_viewportWidget ? m_viewportWidget->hwnd() : nullptr;
}

int NeurusMainWindow::getViewportWidth() const
{
	return m_viewportWidget ? m_viewportWidget->width() : 0;
}

int NeurusMainWindow::getViewportHeight() const
{
	return m_viewportWidget ? m_viewportWidget->height() : 0;
}

VulkanWidget* NeurusMainWindow::getVulkanWidget() const
{
	return m_viewportWidget;
}

// =========================================================================
// Menus
// =========================================================================

void NeurusMainWindow::CreateMenus()
{
	auto* fileMenu = menuBar()->addMenu("&File");

	auto* newAction = fileMenu->addAction("&New");
	newAction->setShortcut(QKeySequence("Ctrl+N"));
	connect(newAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestProjectNew();
	});

	auto* openAction = fileMenu->addAction("&Open...");
	openAction->setShortcut(QKeySequence("Ctrl+O"));
	connect(openAction, &QAction::triggered, []() {
		QString path = QFileDialog::getOpenFileName(
			nullptr, "Open Project", QString(), "Neurus Project (*.neurus.json)");
		if (!path.isEmpty())
			neurus::UIEvents::instance().requestProjectOpen(path);
	});

	auto* saveAction = fileMenu->addAction("&Save");
	saveAction->setShortcut(QKeySequence("Ctrl+S"));
	connect(saveAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestProjectSave();
	});

	auto* saveAsAction = fileMenu->addAction("Save &As...");
	saveAsAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
	connect(saveAsAction, &QAction::triggered, []() {
		QString path = QFileDialog::getSaveFileName(
			nullptr, "Save Project As", QString(), "Neurus Project (*.neurus.json)");
		if (!path.isEmpty())
			neurus::UIEvents::instance().requestProjectSaveAs(path);
	});

	fileMenu->addSeparator();

	auto* exitAction = fileMenu->addAction("E&xit");
	exitAction->setShortcut(QKeySequence::Quit);
	connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

	auto* viewMenu = menuBar()->addMenu("&View");

	auto* saveLayoutAction = viewMenu->addAction("&Save Layout");
	saveLayoutAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
	connect(saveLayoutAction, &QAction::triggered, this, &NeurusMainWindow::SaveLayout);

	auto* resetLayoutAction = viewMenu->addAction("Restore &Default Layout");
	connect(resetLayoutAction, &QAction::triggered, this, &NeurusMainWindow::RestoreDefaultLayout);

	auto* editMenu = menuBar()->addMenu("&Edit");

	auto* addMenu = editMenu->addMenu("&Add");

	auto* meshAction = addMenu->addAction("&Mesh...");
	meshAction->setShortcut(QKeySequence("Ctrl+Shift+M"));
	connect(meshAction, &QAction::triggered, []() {
		QString path = QFileDialog::getOpenFileName(
			nullptr, "Import Mesh", QString(), "OBJ Files (*.obj)");
		if (!path.isEmpty())
			neurus::UIEvents::instance().requestMeshImport(path);
	});

	auto* cameraAction = addMenu->addAction("&Camera");
	connect(cameraAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestCameraAdd();
	});

	auto* lightAction = addMenu->addAction("&Light");
	connect(lightAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestLightAdd();
	});

	auto* toolsMenu = menuBar()->addMenu("&Tools");

	auto* screenshotAction = toolsMenu->addAction("Take &Screenshot");
	screenshotAction->setShortcut(QKeySequence("F12"));
	connect(screenshotAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestScreenshot();
	});

	auto* screenshotAllAction = toolsMenu->addAction("Screenshot All &Passes");
	screenshotAllAction->setShortcut(QKeySequence("Ctrl+F12"));
	connect(screenshotAllAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestScreenshotAll();
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

// =========================================================================
// Docks
// =========================================================================

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
	// --- Viewport (MUST be created FIRST - ADS central widget requirement) ---
	m_viewportDock = new ads::CDockWidget(m_dockManager, "Viewport");
	m_viewportDock->setWidget(makePlaceholder("Viewport"));  // for restoreState matching
	m_viewportDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
	// Use CenterDockWidgetArea instead of setCentralWidget so it stays dockable
	m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_viewportDock);

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

// =========================================================================
// Layout persistence
// =========================================================================

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
