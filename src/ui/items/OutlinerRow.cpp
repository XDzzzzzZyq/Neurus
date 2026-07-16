/**
 * @file OutlinerRow.cpp
 * @brief OutlinerRow implementation — two-phase config: SetObject (data) then SetStyle (visual).
 *
 * Type icons are loaded from the Icons cache as QPixmaps.
 * Visibility toggle buttons swap between visible/invisible icons
 * sourced from the Icons cache on each toggle.
 */

#include "items/OutlinerRow.h"

#include <QFile>
#include <QGraphicsColorizeEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTextStream>

#include <QGuiApplication>

#include "Icons.h"
#include "editor/Input.h"
#include "core/Selections.h"

namespace neurus
{

// =========================================================================
// Outliner stylesheet (loaded once from outliner.qss Qt resource)
// =========================================================================

static QString s_outlinerStyle;

static void LoadOutlinerStyle()
{
	static bool loaded = false;
	if (loaded) return;
	loaded = true;

	QFile file(":/qml/outliner.qss");
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
	s_outlinerStyle = QTextStream(&file).readAll();
	file.close();
}

// =========================================================================
// Constructor — create layout + child widgets once
// =========================================================================

OutlinerRow::OutlinerRow(QWidget* parent)
	: QWidget(parent)
{
	LoadOutlinerStyle();
	setStyleSheet(s_outlinerStyle);

	setFixedHeight(28);

	auto* rowLayout = new QHBoxLayout(this);
	rowLayout->setContentsMargins(4, 1, 4, 1);
	rowLayout->setSpacing(4);

	// --- Type icon (QLabel with QPixmap, 22x22, centered) ---
	m_typeLabel = new QLabel();
	m_typeLabel->setFixedSize(22, 22);
	m_typeLabel->setAlignment(Qt::AlignCenter);
	rowLayout->addWidget(m_typeLabel);

	// --- Name (flat QPushButton styled as label, clickable for selection) ---
	m_nameBtn = new QPushButton();
	m_nameBtn->setObjectName("outlinerNameBtn");
	m_nameBtn->setFlat(true);
	m_nameBtn->setCursor(Qt::PointingHandCursor);
	m_nameBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	// Lambda reads m_objectId at emission time — works after SetObject
	QObject::connect(m_nameBtn, &QPushButton::clicked, this, [this]() {
		const auto mods = Input::GetModifiers(static_cast<uint32_t>(QGuiApplication::queryKeyboardModifiers().toInt()));
		emit objectSelected(ObjectSelected{m_objectId, mods});
	});
	rowLayout->addWidget(m_nameBtn);

	// --- Visibility toggle buttons (icon-driven, checkable) ---
	m_eyeBtn    = new QPushButton();
	m_renderBtn = new QPushButton();

	const QSize kToggleBtnSize(26, 26);
	const QSize kToggleIconSize(20, 20);

	// Eye button
	m_eyeBtn->setObjectName("outlinerToggleBtn");
	m_eyeBtn->setCheckable(true);
	m_eyeBtn->setChecked(true);
	m_eyeBtn->setFlat(true);
	m_eyeBtn->setFixedSize(kToggleBtnSize);
	m_eyeBtn->setIconSize(kToggleIconSize);
	m_eyeBtn->setIcon(Icons::GetIconPair("editor:preview_visible", "editor:preview_invisible"));
	m_eyeBtn->setToolTip(QString::fromUtf8("Viewport visibility"));
	m_eyeBtn->setCursor(Qt::PointingHandCursor);
	auto* eyeFx = new QGraphicsColorizeEffect(m_eyeBtn);
	eyeFx->setColor(QColor("#444444"));  // checked = visible = dark
	eyeFx->setEnabled(true);
	m_eyeBtn->setGraphicsEffect(eyeFx);

	// Render button
	m_renderBtn->setObjectName("outlinerToggleBtn");
	m_renderBtn->setCheckable(true);
	m_renderBtn->setChecked(true);
	m_renderBtn->setFlat(true);
	m_renderBtn->setFixedSize(kToggleBtnSize);
	m_renderBtn->setIconSize(kToggleIconSize);
	m_renderBtn->setIcon(Icons::GetIconPair("editor:render_visible", "editor:render_invisible"));
	m_renderBtn->setToolTip(QString::fromUtf8("Render visibility"));
	m_renderBtn->setCursor(Qt::PointingHandCursor);
	auto* renderFx = new QGraphicsColorizeEffect(m_renderBtn);
	renderFx->setColor(QColor("#444444"));  // checked = visible = dark
	renderFx->setEnabled(true);
	m_renderBtn->setGraphicsEffect(renderFx);

	// Connect signals — lambdas read m_objectId at emission time.
	// Each toggle swaps its icon between visible/invisible variants
	// and applies a colorize tint (dark when checked, light when unchecked).
	QObject::connect(m_eyeBtn, &QPushButton::toggled, this,
		[this](bool viewportChecked) {
			auto* fx = qobject_cast<QGraphicsColorizeEffect*>(m_eyeBtn->graphicsEffect());
			if (fx) fx->setColor(viewportChecked ? QColor("#444444") : QColor("#d0d0d0"));
			emit visibilityChanged(VisibilityChanged{m_objectId, viewportChecked, m_renderBtn->isChecked()});
		});
	QObject::connect(m_renderBtn, &QPushButton::toggled, this,
		[this](bool renderChecked) {
			auto* fx = qobject_cast<QGraphicsColorizeEffect*>(m_renderBtn->graphicsEffect());
			if (fx) fx->setColor(renderChecked ? QColor("#444444") : QColor("#d0d0d0"));
			emit visibilityChanged(VisibilityChanged{m_objectId, m_eyeBtn->isChecked(), renderChecked});
		});

	rowLayout->addWidget(m_eyeBtn);
	rowLayout->addWidget(m_renderBtn);
}

// =========================================================================
// UpdateToggleIcons — refresh both toggle icons from current checked state
// =========================================================================

void OutlinerRow::UpdateToggleIcons()
{
	auto* eyeFx = qobject_cast<QGraphicsColorizeEffect*>(m_eyeBtn->graphicsEffect());
	if (eyeFx) eyeFx->setColor(m_eyeBtn->isChecked() ? QColor("#444444") : QColor("#d0d0d0"));

	auto* renderFx = qobject_cast<QGraphicsColorizeEffect*>(m_renderBtn->graphicsEffect());
	if (renderFx) renderFx->setColor(m_renderBtn->isChecked() ? QColor("#444444") : QColor("#d0d0d0"));
}

// =========================================================================
// SetObject — bind object identity data, reset toggles
// =========================================================================

void OutlinerRow::SetObject(const QIcon& icon,
                            const QString& name, int objectId)
{
	if (m_objectId != objectId){
		m_objectId = objectId;

		m_typeLabel->setPixmap(icon.pixmap(22, 22));
		m_typeLabel->setStyleSheet(QString());  // clear old colored-background CSS
	}

	// Update name text
	m_nameBtn->setText(name);
}

// =========================================================================
// SetVisibilities — set toggle states without emitting signals
// =========================================================================

void OutlinerRow::SetVisibilities(bool viewportVisible, bool renderVisible)
{
	m_eyeBtn->blockSignals(true);
	m_eyeBtn->setChecked(viewportVisible);
	m_eyeBtn->blockSignals(false);
	m_renderBtn->blockSignals(true);
	m_renderBtn->setChecked(renderVisible);
	m_renderBtn->blockSignals(false);

	UpdateToggleIcons();
}

// =========================================================================
// SetStyle — apply selection text color + alternating row background
// =========================================================================

void OutlinerRow::SetStyle(int mode, int rowIndex)
{
	
	// --- Selection text color ---
	if (m_mode != mode){
		QString color;
		if (mode & static_cast<int>(neurus::SelectionMode::Active))
			color = QString::fromUtf8("#ff6f00");    // orange — active
		else if (mode & static_cast<int>(neurus::SelectionMode::Selected))	
			color = QString::fromUtf8("#4A90D9");    // blue — selected, not active
		else
			color = QString::fromUtf8("#000000");    // black — deselected
		m_nameBtn->setStyleSheet(
			QString::fromUtf8("QPushButton { color: %1; }").arg(color));

		m_mode = mode;
	}

	// --- Alternating row background ---
	if (m_idx != rowIndex)
	{
		if (rowIndex % 2 == 1)
		{
			QPalette pal = palette();
			pal.setColor(QPalette::Window, QColor(255, 255, 255, 100));
			setPalette(pal);
			setAutoFillBackground(true);
		}
		else
		{
			setAutoFillBackground(false);
		}
		m_idx = rowIndex;
	}
}

} // namespace neurus
