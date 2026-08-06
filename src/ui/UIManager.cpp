#include "UIManager.h"
#include "Icons.h"
#include "panels/LogPanel.h"
#include "panels/Outliner.h"
#include "panels/ProfilingPanel.h"
#include "panels/PropertyPanel.h"
#include "panels/RenderConfigPanel.h"
#include "panels/ShaderEditorPanel.h"
#include "panels/Viewport.h"
#include "UIContext.h"

#include "editor/events/UIEvents.h"

#include <QApplication>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

#include <memory>

namespace neurus {

// =========================================================================
// Constructor / Destructor
// =========================================================================

UIManager::UIManager(QWidget* parent)
	: QMainWindow(parent)
{
	Icons::Initialize();

	setWindowTitle("Neurus");
	resize(1600, 900);

	// Disable opaque splitter resize for better Vulkan window container behavior
	ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, false);
	ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);

	win_dockManager = new ads::CDockManager(this);

	CreateDocks();
	CreateMenus();
}

UIManager::~UIManager()
{
	// Release panel/dock pointers before Qt parent-child cleanup destroys them.
	m_panels.clear();
	m_docks.clear();
}

// =========================================================================
// Viewport accessors
// =========================================================================

NativeWindowHandle UIManager::getViewportHwnd() const
{
	auto* vp = GetPanel<Viewport>();
	return vp ? vp->hwnd() : nullptr;
}

int UIManager::getViewportWidth() const
{
	auto* vp = GetPanel<Viewport>();
	return vp ? vp->width() : 0;
}

int UIManager::getViewportHeight() const
{
	auto* vp = GetPanel<Viewport>();
	return vp ? vp->height() : 0;
}

// =========================================================================
// Refresh – push UIContext to all registered panels
// =========================================================================

void UIManager::Refresh(const UIContext& ctx)
{
	for (auto& [type, widget] : m_panels)
	{
		auto* panel = qobject_cast<UIPanel*>(widget);
		panel->Refresh(ctx);
	}
}

// =========================================================================
// Menus
// =========================================================================

void UIManager::CreateMenus()
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

	auto* resetLayoutAction = viewMenu->addAction("Restore &Default Layout");
	connect(resetLayoutAction, &QAction::triggered, this, &UIManager::RestoreDefaultLayout);

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

	auto* lightSubmenu = addMenu->addMenu("&Light");

	auto* pointLightAction = lightSubmenu->addAction("&Point Light");
	connect(pointLightAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestLightAdd();
	});

	auto* sunLightAction = lightSubmenu->addAction("&Sun Light");
	connect(sunLightAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestSunLightAdd();
	});

	auto* spotLightAction = lightSubmenu->addAction("Spo&t Light");
	connect(spotLightAction, &QAction::triggered, []() {
		neurus::UIEvents::instance().requestSpotLightAdd();
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

void UIManager::CreateDocks()
{
	// --- Viewport (MUST be created FIRST - ADS central widget requirement) ---
	auto viewport = std::make_unique<Viewport>();
	viewport->resize(800, 600);
	viewport->winId();  // Force native window handle creation
	NativeWindowHandle newHwnd = viewport->hwnd();

	auto* viewportDock = new ads::CDockWidget(win_dockManager, viewport->PanelName());
	viewportDock->setWidget(viewport.get(), ads::CDockWidget::ForceNoScrollArea);
	viewportDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
	win_dockManager->addDockWidget(ads::LeftDockWidgetArea, viewportDock);

	// Transfer panel ownership to CDockWidget (Qt parent-child).
	// After setWidget(), the dock owns the panel; release unique_ptr to avoid double-delete.
	m_panels[PanelType::Viewport] = viewport.release();
	m_docks[PanelType::Viewport] = viewportDock;

	// --- Left: Shader Editor ---
	auto shaderEditor = std::make_unique<ShaderEditorPanel>();
	auto* shaderDock = new ads::CDockWidget(win_dockManager, shaderEditor->PanelName());
	shaderDock->setWidget(shaderEditor.get(), ads::CDockWidget::ForceNoScrollArea);
	shaderDock->resize(280, 300);
	shaderDock->setMinimumSize(200, 200);
	win_dockManager->addDockWidget(ads::LeftDockWidgetArea, shaderDock);
	m_panels[PanelType::ShaderEditor] = shaderEditor.release();
	m_docks[PanelType::ShaderEditor] = shaderDock;

	// --- Left: Outliner ---
	auto outliner = std::make_unique<Outliner>();
	auto* outlinerDock = new ads::CDockWidget(win_dockManager, outliner->PanelName());
	outlinerDock->setWidget(outliner.get());
	outlinerDock->resize(280, 300);
	outlinerDock->setMinimumSize(200, 200);
	win_dockManager->addDockWidget(ads::LeftDockWidgetArea, outlinerDock);
	m_panels[PanelType::Outliner] = outliner.release();
	m_docks[PanelType::Outliner] = outlinerDock;

	// --- Right: Property Panel ---
	auto propertyPanel = std::make_unique<PropertyPanel>();
	auto* propDock = new ads::CDockWidget(win_dockManager, propertyPanel->PanelName());
	propDock->setWidget(propertyPanel.get());
	propDock->resize(280, 300);
	propDock->setMinimumSize(200, 200);
	win_dockManager->addDockWidget(ads::RightDockWidgetArea, propDock, outlinerDock->dockAreaWidget());
	m_panels[PanelType::PropertyPanel] = propertyPanel.release();
	m_docks[PanelType::PropertyPanel] = propDock;

	// --- Right: Render Config ---
	auto renderConfigPanel = std::make_unique<RenderConfigPanel>();
	auto* configDock = new ads::CDockWidget(win_dockManager, renderConfigPanel->PanelName());
	configDock->setWidget(renderConfigPanel.get(), ads::CDockWidget::ForceNoScrollArea);
	configDock->resize(280, 400);
	configDock->setMinimumSize(220, 300);
	win_dockManager->addDockWidget(ads::RightDockWidgetArea, configDock, outlinerDock->dockAreaWidget());
	m_panels[PanelType::RenderConfig] = renderConfigPanel.release();
	m_docks[PanelType::RenderConfig] = configDock;

	// --- Bottom: Profiling ---
	auto profilingPanel = std::make_unique<ProfilingPanel>();
	auto* profilingDock = new ads::CDockWidget(win_dockManager, profilingPanel->PanelName());
	profilingDock->setWidget(profilingPanel.get(), ads::CDockWidget::ForceNoScrollArea);
	profilingDock->resize(640, 220);
	profilingDock->setMinimumSize(320, 140);
	win_dockManager->addDockWidget(ads::BottomDockWidgetArea, profilingDock);
	m_panels[PanelType::Profiling] = profilingPanel.release();
	m_docks[PanelType::Profiling] = profilingDock;

	// --- Bottom: Texture Viewer (placeholder, not a UIPanel) ---
	auto* textureDock = new ads::CDockWidget(win_dockManager, "Texture Viewer");
	textureDock->setWidget(makePlaceholder("Texture Viewer"));
	textureDock->resize(300, 200);
	textureDock->setMinimumSize(200, 150);
	win_dockManager->addDockWidget(ads::BottomDockWidgetArea, textureDock);

	// --- Bottom: Log Panel (tabbed onto Texture Viewer area) ---
	auto logPanel = std::make_unique<neurus::LogPanel>();
	auto* logPanelRaw = logPanel.get();
	auto* logDock = new ads::CDockWidget(win_dockManager, logPanel->PanelName());
	logDock->setWidget(logPanel.get(), ads::CDockWidget::ForceNoScrollArea);
	logDock->resize(640, 220);
	logDock->setMinimumSize(320, 140);
	win_dockManager->addDockWidget(ads::BottomDockWidgetArea, logDock,
	                               textureDock->dockAreaWidget());
	m_panels[PanelType::Log] = logPanel.release();
	m_docks[PanelType::Log] = logDock;

	// Error notifier: LogPanel signal -> status bar (3s, throttled by delta)
	QObject::connect(logPanelRaw, &neurus::LogPanel::errorNotified,
	                 this, [this](int delta, const QString& first) {
	                     statusBar()->showMessage(
	                         tr("%1 new error(s): %2").arg(delta).arg(first), 3000);
	                 });

	// Notify Application of the new native window handle for surface recreation
	UIEvents::instance().requestUIRecreation(reinterpret_cast<quintptr>(newHwnd));
}

// =========================================================================
// Layout persistence
// =========================================================================

std::string UIManager::ExportLayout() const
{
	QByteArray geom  = saveGeometry();
	QByteArray state = win_dockManager->saveState();
	QByteArray packed = geom.toBase64() + '\n' + state.toBase64();
	return packed.toStdString();
}

void UIManager::ApplyLayout(const std::string& blob)
{
	if (blob.empty()) return;
	QByteArray packed = QByteArray::fromStdString(blob);
	int nl = packed.indexOf('\n');
	if (nl < 0) return;
	QByteArray geom  = QByteArray::fromBase64(packed.left(nl));
	QByteArray state = QByteArray::fromBase64(packed.mid(nl + 1));
	if (!geom.isEmpty())  restoreGeometry(geom);
	if (!state.isEmpty()) win_dockManager->restoreState(state);
}

void UIManager::RestoreDefaultLayout()
{
	// Clear panel/dock maps before dock manager destroys its children.
	// Dock widgets and their contained panels are owned by CDockManager
	// via Qt parent-child; clearing the maps just drops our non-owning pointers.
	m_panels.clear();
	m_docks.clear();

	auto docks = win_dockManager->dockWidgetsMap();
	for (auto it = docks.begin(); it != docks.end(); ++it)
	{
		it.value()->deleteDockWidget();
	}

	// Re-create the default dock arrangement (emits uiRecreated)
	CreateDocks();
}

} // namespace neurus
