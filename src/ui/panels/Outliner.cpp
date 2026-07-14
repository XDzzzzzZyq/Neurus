/**
 * @file Outliner.cpp
 * @brief Outliner implementation — pool-managed row list from UIContext scene data.
 *
 * Architecture:
 * - Constructor sets up the QScrollArea + container layout (empty).
 * - Refresh() reads scene objects from UIContext and reconfigures rows
 *   via OutlinerRow::SetObject() from a growing pool.
 * - New rows are created when pool < scene objects, and connected to
 *   Outliner signals once. Extra rows are hidden (not destroyed).
 * - Signal lambdas on OutlinerRow read m_objectId at emission time,
 *   so recycling a row to a different object is transparent.
 */

#include "Outliner.h"

#include "UIContext.h"
#include "items/OutlinerRow.h"

#include "scene/Scene.h"
#include "scene/UID.h"  // ObjectID, GOType

#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <string>

namespace neurus
{

// =========================================================================
// Type-info helpers (mapped from GOType, stateless)
// =========================================================================

namespace
{

/**
 * @brief Returns the display letter and CSS background color for a GOType.
 */
static std::pair<QString, QString> TypeInfo(ObjectID::GOType type)
{
	switch (type)
	{
	case ObjectID::GOType::GO_CAM:
		return {QString::fromUtf8("C"),  QString::fromUtf8("#4A90D9")};  // blue
	case ObjectID::GOType::GO_LIGHT:
		return {QString::fromUtf8("L"),  QString::fromUtf8("#F5A623")};  // amber
	case ObjectID::GOType::GO_POLYLIGHT:
		return {QString::fromUtf8("P"),  QString::fromUtf8("#F5A623")};  // amber
	case ObjectID::GOType::GO_MESH:
		return {QString::fromUtf8("M"),  QString::fromUtf8("#7ED321")};  // green
	case ObjectID::GOType::GO_ENVIR:
		return {QString::fromUtf8("E"),  QString::fromUtf8("#9B59B6")};  // purple
	case ObjectID::GOType::GO_SPRITE:
		return {QString::fromUtf8("S"),  QString::fromUtf8("#1ABC9C")};  // cyan
	case ObjectID::GOType::GO_DL:
	case ObjectID::GOType::GO_DP:
	case ObjectID::GOType::GO_DM:
		return {QString::fromUtf8("D"),  QString::fromUtf8("#95A5A6")};  // gray
	case ObjectID::GOType::GO_SDFFIELD:
		return {QString::fromUtf8("F"),  QString::fromUtf8("#E67E22")};  // orange
	default:
		return {QString::fromUtf8("?"),  QString::fromUtf8("#95A5A6")};  // gray
	}
}

} // anonymous namespace

// =========================================================================
// Constructor — scroll area + empty container (no hard-coded rows)
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
// EnsureRowPool — grow pool to meet needed capacity
// =========================================================================

void Outliner::EnsureRowPool(std::size_t needed)
{
	while (m_rowPool.size() < needed)
	{
		auto* row = new OutlinerRow(m_sceneGroup);

		// Connect row signals to Outliner signals once (permanent).
		// Lambdas read m_objectId at emission time, so recycling
		// a row to a new objectId works without reconnecting.
		QObject::connect(row, &OutlinerRow::objectSelected,
			this, &Outliner::objectSelected);
		QObject::connect(row, &OutlinerRow::visibilityChanged,
			this, &Outliner::visibilityChanged);

		m_groupLayout->addWidget(row);
		m_rowPool.push_back(row);
	}
}

// =========================================================================
// Refresh — bind pool rows to scene objects
// =========================================================================

void Outliner::Refresh(const UIContext& ctx)
{
	auto ids = ctx.GetObjectIDs();

	// Create the "Scene" category group once.
	if (!m_sceneGroup)
	{
		m_sceneGroup = AddCategoryGroup(QString::fromUtf8("Scene"));
		m_groupLayout = qobject_cast<QVBoxLayout*>(m_sceneGroup->layout());
	}

	// --- Query selection state from the scene's Selections<const ObjectID*> ---
	const Scene* scene = static_cast<const Scene*>(ctx.scene);
	const ObjectID* activeObj = nullptr;
	if (scene)
	{
		activeObj = scene->selections.GetActiveObject();
	}

	// Count valid (non-null) objects.
	std::size_t validCount = 0;
	for (const auto* obj : ids)
	{
		if (obj) ++validCount;
	}

	// Ensure pool is large enough for valid objects.
	EnsureRowPool(validCount);

	// Configure visible rows.
	std::size_t poolIndex = 0;
	int rowIndex = 0;
	for (const auto* obj : ids)
	{
		if (!obj) continue;

		auto [letter, color] = TypeInfo(obj->o_type);

		// Phase 1 — bind object identity data (name, id, icon).
		m_rowPool[poolIndex]->SetObject(
			letter, color,
			QString::fromStdString(obj->o_name),
			obj->GetObjectID());

		// Phase 2 — apply visual styling (selection highlight + row bg).
		bool isActive   = (activeObj == obj);
		bool isSelected = scene && scene->selections.IsSelected(obj);
		m_rowPool[poolIndex]->SetStyle(isActive, isSelected && !isActive, rowIndex);

		m_rowPool[poolIndex]->setVisible(true);
		++poolIndex;
		++rowIndex;
	}

	// Hide surplus rows (pool larger than current scene).
	for (std::size_t i = validCount; i < m_rowPool.size(); ++i)
	{
		m_rowPool[i]->setVisible(false);
	}

	// Show/hide the category group based on whether we have objects.
	m_sceneGroup->setVisible(validCount > 0);
}

} // namespace neurus
