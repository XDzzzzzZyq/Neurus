/**
 * @file Outliner.cpp
 * @brief Outliner implementation — Blender-style vertical list with type icons,
 *        names, and visibility toggles.
 *
 * Phase 1: Hard-coded demo layout following the RenderConfigPanel constructor
 * pattern (QVBoxLayout → QScrollArea → container → vertical row list).
 * Future phases will wire to real Scene data via UIContext.
 */

#include "Outliner.h"

#include "UIContext.h"

#include <QColor>
#include <QGroupBox>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace neurus
{

// =========================================================================
// Constructor — follows RenderConfigPanel pattern
// =========================================================================

Outliner::Outliner(QWidget* parent)
	: UIPanel(PanelType::Outliner, QString(), parent)
{
	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);

	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	mainLayout->addWidget(m_scrollArea);

	m_container = new QWidget();
	m_listLayout = new QVBoxLayout(m_container);
	m_listLayout->setContentsMargins(4, 4, 4, 4);
	m_listLayout->setSpacing(2);
	m_listLayout->setAlignment(Qt::AlignTop);

	m_scrollArea->setWidget(m_container);

	BuildUI();
}

// =========================================================================
// BuildUI — hard-coded demo rows
// =========================================================================

void Outliner::BuildUI()
{
	// Type colors matching GOType categories (blender-esque palette)
	const QString kCamColor   = QString::fromUtf8("#4A90D9");  // blue
	const QString kLightColor = QString::fromUtf8("#F5A623");  // amber
	const QString kMeshColor  = QString::fromUtf8("#7ED321");  // green

	auto* sceneGroup = AddCategoryGroup(QString::fromUtf8("Scene"));
	auto* sceneLayout = qobject_cast<QVBoxLayout*>(sceneGroup->layout());

	// --- Cameras ---
	sceneLayout->addWidget(CreateRow(
		QString::fromUtf8("C"), kCamColor,
		QString::fromUtf8("Main Camera"), 1001, 0));

	sceneLayout->addWidget(CreateRow(
		QString::fromUtf8("C"), kCamColor,
		QString::fromUtf8("Side Camera"), 1002, 1));

	// --- Lights ---
	sceneLayout->addWidget(CreateRow(
		QString::fromUtf8("L"), kLightColor,
		QString::fromUtf8("Sun Light"), 2001, 2));

	sceneLayout->addWidget(CreateRow(
		QString::fromUtf8("L"), kLightColor,
		QString::fromUtf8("Point Light"), 2002, 3));

	// --- Meshes ---
	sceneLayout->addWidget(CreateRow(
		QString::fromUtf8("M"), kMeshColor,
		QString::fromUtf8("Sphere"), 3001, 4));

	sceneLayout->addWidget(CreateRow(
		QString::fromUtf8("M"), kMeshColor,
		QString::fromUtf8("Ground Plane"), 3002, 5));

	m_listLayout->addStretch();
}

// =========================================================================
// AddCategoryGroup — creates a QGroupBox with a QVBoxLayout for rows
// =========================================================================

QGroupBox* Outliner::AddCategoryGroup(const QString& title)
{
	auto* group = new QGroupBox(title);
	//group->setFlat(true);

	auto* groupLayout = new QVBoxLayout(group);
	groupLayout->setContentsMargins(4, 4, 4, 4);
	groupLayout->setSpacing(2);

	m_listLayout->addWidget(group);
	return group;
}

// =========================================================================
// CreateRow — single row: [type icon] [name] [eye toggle] [monitor toggle]
// =========================================================================

QWidget* Outliner::CreateRow(const QString& typeLetter, const QString& typeColor,
                              const QString& name, int objectId, int rowIndex)
{
	auto* row = new QWidget();
	row->setFixedHeight(28);
	row->setProperty("objectId", objectId);

	// Alternating row backgrounds for readability
	if (rowIndex % 2 == 1)
	{
		QPalette pal = row->palette();
		pal.setColor(QPalette::Window, QColor(255, 255, 255, 100));
		row->setPalette(pal);
		row->setAutoFillBackground(true);
	}

	auto* rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(4, 1, 4, 1);
	rowLayout->setSpacing(4);

	// --- Type icon (colored QLabel, 22x22, centered) ---
	auto* typeLabel = new QLabel(typeLetter);
	typeLabel->setFixedSize(22, 22);
	typeLabel->setAlignment(Qt::AlignCenter);
	typeLabel->setStyleSheet(QString(
		"QLabel {"
		"  background-color: %1;"
		"  color: white;"
		"  border-radius: 3px;"
		"  font-size: 11px;"
		"  font-weight: bold;"
		"}").arg(typeColor));
	rowLayout->addWidget(typeLabel);

	// --- Name (flat QPushButton styled as label, clickable for selection) ---
	auto* nameBtn = new QPushButton(name);
	nameBtn->setFlat(true);
	nameBtn->setCursor(Qt::PointingHandCursor);
	nameBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	nameBtn->setStyleSheet(
		"QPushButton {"
		"  text-align: left;"
		"  border: 1px solid transparent;"
		"  border-radius: 10px;"
		"  background: transparent;"
		"  padding: 1px 4px;"
		"  font-size: 11px;"
		"}"
		"QPushButton:hover {"
		"  color: #000000;"
		"  border-color: #313131;"
		"}"
		"QPushButton:pressed {"
		"  color: #676767;"
		"  background: rgba(0, 0, 0, 0.1);"
		"}");
	QObject::connect(nameBtn, &QPushButton::clicked, this, [this, objectId]() {
		emit objectSelected(objectId);
	});
	rowLayout->addWidget(nameBtn);

	// --- Visibility toggle buttons ---
	//
	// Flat QPushButton with checkable state — larger hit target than QToolButton.
	// Both must exist before either connect lambda is created.
	//
	static const QString kToggleStyle = QString::fromUtf8(
		"QPushButton {"
		"  border: 1px solid transparent;"
		"  border-radius: 3px;"
		"  background: transparent;"
		"  padding: 2px;"
		"  font-size: 20px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:!checked {"
		"  color: #d0d0d0;"
		"}"
		"QPushButton:checked {"
		"  color: #444444;"
		"}"
		"QPushButton:hover {"
		"  background: rgba(255, 255, 255, 0.10);"
		"  border-color: rgba(255, 255, 255, 0.15);"
		"}");

	auto* eyeBtn    = new QPushButton(QString::fromUtf8("\u25C9"));  // ◉
	auto* renderBtn = new QPushButton(QString::fromUtf8("\u25A3"));  // ▣

	// Eye button
	eyeBtn->setCheckable(true);
	eyeBtn->setChecked(true);
	eyeBtn->setFlat(true);
	eyeBtn->setFixedSize(28, 26);
	eyeBtn->setToolTip(QString::fromUtf8("Viewport visibility"));
	eyeBtn->setCursor(Qt::PointingHandCursor);
	eyeBtn->setStyleSheet(kToggleStyle);

	// Render button
	renderBtn->setCheckable(true);
	renderBtn->setChecked(true);
	renderBtn->setFlat(true);
	renderBtn->setFixedSize(28, 26);
	renderBtn->setToolTip(QString::fromUtf8("Render visibility"));
	renderBtn->setCursor(Qt::PointingHandCursor);
	renderBtn->setStyleSheet(kToggleStyle);

	// Connect signals — capture the other button for combined state
	QObject::connect(eyeBtn, &QPushButton::toggled, this,
		[this, objectId, renderBtn](bool viewportChecked) {
			emit visibilityChanged(objectId, viewportChecked, renderBtn->isChecked());
		});
	QObject::connect(renderBtn, &QPushButton::toggled, this,
		[this, objectId, eyeBtn](bool renderChecked) {
			emit visibilityChanged(objectId, eyeBtn->isChecked(), renderChecked);
		});

	rowLayout->addWidget(eyeBtn);
	rowLayout->addWidget(renderBtn);

	return row;
}

// =========================================================================
// Refresh — no-op for hard-coded demo (future: rebuild from UIContext)
// =========================================================================

void Outliner::Refresh(const UIContext& /*ctx*/)
{
	// Hard-coded demo data — no per-frame refresh needed.
	// Future: rebuild rows from UIContext scene data when scene changes.
}

} // namespace neurus
