/**
 * @file OutlinerRow.cpp
 * @brief OutlinerRow implementation — constructor creates widgets once,
 *        SetObject() reconfigure for any scene object (pool-friendly).
 */

#include "items/OutlinerRow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>

namespace neurus
{

// =========================================================================
// Constructor — create layout + child widgets once
// =========================================================================

OutlinerRow::OutlinerRow(QWidget* parent)
	: QWidget(parent)
{
	setFixedHeight(28);

	auto* rowLayout = new QHBoxLayout(this);
	rowLayout->setContentsMargins(4, 1, 4, 1);
	rowLayout->setSpacing(4);

	// --- Type icon (colored QLabel, 22x22, centered) ---
	m_typeLabel = new QLabel();
	m_typeLabel->setFixedSize(22, 22);
	m_typeLabel->setAlignment(Qt::AlignCenter);
	rowLayout->addWidget(m_typeLabel);

	// --- Name (flat QPushButton styled as label, clickable for selection) ---
	m_nameBtn = new QPushButton();
	m_nameBtn->setFlat(true);
	m_nameBtn->setCursor(Qt::PointingHandCursor);
	m_nameBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_nameBtn->setStyleSheet(
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
	// Lambda reads m_objectId at emission time — works after SetObject
	QObject::connect(m_nameBtn, &QPushButton::clicked, this, [this]() {
		emit objectSelected(m_objectId);
	});
	rowLayout->addWidget(m_nameBtn);

	// --- Visibility toggle buttons ---
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

	m_eyeBtn    = new QPushButton(QString::fromUtf8("\u25C9"));  // ◉
	m_renderBtn = new QPushButton(QString::fromUtf8("\u25A3"));  // ▣

	// Eye button
	m_eyeBtn->setCheckable(true);
	m_eyeBtn->setChecked(true);
	m_eyeBtn->setFlat(true);
	m_eyeBtn->setFixedSize(28, 26);
	m_eyeBtn->setToolTip(QString::fromUtf8("Viewport visibility"));
	m_eyeBtn->setCursor(Qt::PointingHandCursor);
	m_eyeBtn->setStyleSheet(kToggleStyle);

	// Render button
	m_renderBtn->setCheckable(true);
	m_renderBtn->setChecked(true);
	m_renderBtn->setFlat(true);
	m_renderBtn->setFixedSize(28, 26);
	m_renderBtn->setToolTip(QString::fromUtf8("Render visibility"));
	m_renderBtn->setCursor(Qt::PointingHandCursor);
	m_renderBtn->setStyleSheet(kToggleStyle);

	// Connect signals — lambdas read m_objectId at emission time
	QObject::connect(m_eyeBtn, &QPushButton::toggled, this,
		[this](bool viewportChecked) {
			emit visibilityChanged(m_objectId, viewportChecked, m_renderBtn->isChecked());
		});
	QObject::connect(m_renderBtn, &QPushButton::toggled, this,
		[this](bool renderChecked) {
			emit visibilityChanged(m_objectId, m_eyeBtn->isChecked(), renderChecked);
		});

	rowLayout->addWidget(m_eyeBtn);
	rowLayout->addWidget(m_renderBtn);
}

// =========================================================================
// SetObject — reconfigure an existing row for a different scene object
// =========================================================================

void OutlinerRow::SetObject(const QString& typeLetter, const QString& typeColor,
                            const QString& name, int objectId, int rowIndex)
{
	m_objectId = objectId;
	setProperty("objectId", objectId);

	// Alternating row backgrounds for readability
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

	// Update type icon
	m_typeLabel->setText(typeLetter);
	m_typeLabel->setStyleSheet(QString(
		"QLabel {"
		"  background-color: %1;"
		"  color: white;"
		"  border-radius: 3px;"
		"  font-size: 11px;"
		"  font-weight: bold;"
		"}").arg(typeColor));

	// Update name text
	m_nameBtn->setText(name);

	// Reset visibility toggles to default (visible)
	m_eyeBtn->setChecked(true);
	m_renderBtn->setChecked(true);
}

} // namespace neurus
