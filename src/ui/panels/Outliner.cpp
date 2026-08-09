/**
 * @file Outliner.cpp
 * @brief Outliner implementation — pool-managed row list from UIContext scene data.
 *
 * Architecture:
 * - Constructor sets up the QScrollArea + container layout (empty).
 * - Refresh() reads scene objects from UIContext and reconfigures rows
 *   via OutlinerRow::setObject() from a growing pool.
 * - New rows are created when pool < scene objects, and connected to
 *   Outliner signals once. Extra rows are hidden (not destroyed).
 * - Signal lambdas on OutlinerRow read m_object at emission time,
 *   so recycling a row to a different object is transparent.
 * - Type icons are resolved from GOType via the Outliner-owned Icons cache
 *   and passed as icon names ("scene:camera", "scene:light", etc.).
 */

#include "Outliner.h"

#include "Icons.h"
#include "UIContext.h"
#include "items/OutlinerRow.h"

#include "scene/Scene.h"
#include "scene/ObjectID.h"  // ObjectID, GOType

#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <string>

namespace neurus
{

// =========================================================================
// Constructor — scroll area + empty container (no hard-coded rows)
// =========================================================================

Outliner::Outliner(QWidget* parent)
	: UIPanel(PanelType::Outliner, QString(), parent)
{
	setFocusPolicy(Qt::StrongFocus);

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

	m_sceneGroup = AddCategoryGroup(QString::fromUtf8("Scene"));
	m_groupLayout = qobject_cast<QVBoxLayout*>(m_sceneGroup->layout());
}

// =========================================================================
// AddCategoryGroup — creates a QGroupBox with a QVBoxLayout for rows
// =========================================================================

QGroupBox* Outliner::AddCategoryGroup(const QString& title)
{
	auto* group = new QGroupBox(title);

	auto* groupLayout = new QVBoxLayout(group);
	groupLayout->setContentsMargins(4, 4, 4, 4);
	groupLayout->setSpacing(2);

	m_listLayout->addWidget(group);
	return group;
}

// =========================================================================
// keyPressEvent — Delete = delete all selected objects
// =========================================================================

void Outliner::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Delete)
	{
		// Pure intent: the Editor wraps the active scene and forwards the
		// batched ObjectDeleteRequested to the SceneController.
		emit deleteRequested(DeleteRequested{});
		event->accept();
		return;
	}
	QWidget::keyPressEvent(event);
}

// =========================================================================
// EnsureRowPool — grow pool to meet needed capacity
// =========================================================================

void Outliner::EnsureRowPool(std::size_t needed)
{
	while (m_rowPool.size() < needed)
	{
		auto* row = new OutlinerRow(m_sceneGroup);

		// Connect row signals to Outliner signals once (permanent).
		// Lambdas read m_object at emission time, so recycling
		// a row to a new object works without reconnecting.
		QObject::connect(row, &OutlinerRow::objectClicked,
			this, &Outliner::objectClicked);
		QObject::connect(row, &OutlinerRow::visibilityChanged,
			this, &Outliner::visibilityChanged);

		m_groupLayout->addWidget(row);
		m_rowPool.push_back(row);
	}

	// m_rowPool.size() >= needed

	// Hide surplus rows (pool larger than current scene).
	for (std::size_t i = needed; i < m_rowPool.size(); ++i)
	{
		m_rowPool[i]->setVisible(false);
	}
}

// =========================================================================
// Refresh — bind pool rows to scene objects
// =========================================================================

void Outliner::Refresh(const UIContext& ctx)
{
	auto ids = ctx.GetObjectIDs();

	// --- Query selection state from the scene's Selections<const ObjectID*> ---
	const Scene* scene = static_cast<const Scene*>(ctx.editor.scene);

	// Ensure pool is large enough for valid objects.
	EnsureRowPool(ids.size());

	// Configure visible rows.
	std::size_t poolIndex = 0;
	for (const auto* obj : ids)
	{
		// Phase 1 — bind object identity data (icon, name, pointer).
		m_rowPool[poolIndex]->setObject(
			Icons::ObjectIcon(static_cast<int>(obj->o_type)),
			QString::fromStdString(obj->o_name),
			obj);

		// Sync visibility toggles to the object's actual flags.
		m_rowPool[poolIndex]->setVisibilities(obj->is_viewport, obj->is_rendered);

		// Phase 2 — apply selection highlight (QSS property) + row background.
		neurus::SelectionMode mode = scene->selections.GetMode(obj);
		m_rowPool[poolIndex]->setSelectionMode(mode);
		m_rowPool[poolIndex]->setRowIndex(static_cast<int>(poolIndex));

		m_rowPool[poolIndex]->setVisible(true);
		++poolIndex;
	}

	// Show/hide the category group based on whether we have objects.
	m_sceneGroup->setVisible(!ids.empty());
}

} // namespace neurus
