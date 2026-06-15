#include "NeurusMainWindow.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QVBoxLayout>

namespace neurus {

NeurusMainWindow::NeurusMainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	setWindowTitle("Neurus");
	resize(1600, 900);

	CreateDocks();
	CreateMenus();
}

NeurusMainWindow::~NeurusMainWindow() = default;

// --- Menu Bar ---

void NeurusMainWindow::CreateMenus()
{
	// --- File Menu ---
	auto* fileMenu = menuBar()->addMenu("&File");

	auto* exitAction = fileMenu->addAction("E&xit");
	exitAction->setShortcut(QKeySequence::Quit);
	connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

	// --- View Menu ---
	auto* viewMenu = menuBar()->addMenu("&View");

	const auto docks = findChildren<QDockWidget*>();
	for (auto* dock : docks)
	{
		viewMenu->addAction(dock->toggleViewAction());
	}

	// --- Help Menu ---
	auto* helpMenu = menuBar()->addMenu("&Help");

	auto* aboutAction = helpMenu->addAction("&About Neurus");
	connect(aboutAction, &QAction::triggered, this, [this]()
	{
		QMessageBox::about(this, "About Neurus",
			"<h2>Neurus</h2>"
			"<p>A C++20 Vulkan-HPP 1.4 real-time renderer.</p>"
			"<p>Version 0.1.0 (Triangle MVP)</p>");
	});
}

// --- Viewport Dock ---

QDockWidget* NeurusMainWindow::createViewportDock(QWidget* vulkanWidget)
{
	auto* dock = new QDockWidget("Viewport", this);
	dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);
	dock->setWidget(vulkanWidget);

	// Add to left dock area initially, below Outliner
	addDockWidget(Qt::LeftDockWidgetArea, dock);

	return dock;
}

// --- Dock Layout ---

void NeurusMainWindow::CreateDocks()
{
	constexpr auto kDockFeatures = QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable;

	// Helper: create a labeled placeholder widget inside a dock
	auto makePlaceholder = [](QDockWidget* dock, const QString& text)
	{
		auto* widget = new QWidget(dock);
		auto* layout = new QVBoxLayout(widget);
		auto* label = new QLabel(text, widget);
		label->setAlignment(Qt::AlignCenter);
		layout->addWidget(label);
		dock->setWidget(widget);
	};

	// --- Left dock: Outliner ---
	auto* outlinerDock = new QDockWidget("Outliner", this);
	outlinerDock->setFeatures(kDockFeatures);
	outlinerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	makePlaceholder(outlinerDock, "Outliner");
	addDockWidget(Qt::LeftDockWidgetArea, outlinerDock);

	// --- Right dock: Property Editor ---
	auto* propEditorDock = new QDockWidget("Property Editor", this);
	propEditorDock->setFeatures(kDockFeatures);
	propEditorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	makePlaceholder(propEditorDock, "Property Editor");
	addDockWidget(Qt::RightDockWidgetArea, propEditorDock);

	// --- Right dock: Render Config (tabified with Property Editor) ---
	auto* renderConfigDock = new QDockWidget("Render Config", this);
	renderConfigDock->setFeatures(kDockFeatures);
	renderConfigDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	makePlaceholder(renderConfigDock, "Render Config");
	addDockWidget(Qt::RightDockWidgetArea, renderConfigDock);
	tabifyDockWidget(propEditorDock, renderConfigDock);

	// --- Bottom dock: Shader Editor ---
	auto* shaderEditorDock = new QDockWidget("Shader Editor", this);
	shaderEditorDock->setFeatures(kDockFeatures);
	shaderEditorDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
	makePlaceholder(shaderEditorDock, "Shader Editor");
	addDockWidget(Qt::BottomDockWidgetArea, shaderEditorDock);

	// --- Bottom dock: Texture Viewer (tabified with Shader Editor) ---
	auto* textureViewerDock = new QDockWidget("Texture Viewer", this);
	textureViewerDock->setFeatures(kDockFeatures);
	textureViewerDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
	makePlaceholder(textureViewerDock, "Texture Viewer");
	addDockWidget(Qt::BottomDockWidgetArea, textureViewerDock);
	tabifyDockWidget(shaderEditorDock, textureViewerDock);

	// Raise the first dock in each tab group to be the visible one
	propEditorDock->raise();
	shaderEditorDock->raise();
}

} // namespace neurus
