/**
 * @file Outliner.h
 * @brief Outliner dock panel — Blender-style vertical list view.
 *
 * Displays scene objects as a scrollable vertical list. Each row shows the
 * object type icon, name, and two visibility toggles (viewport / render).
 *
 * Phase 1: Hard-coded demo layout following the RenderConfigPanel pattern.
 * Object IDs are stored via QWidget::setProperty but not displayed.
 *
 * Architecture:
 * - UIPanel subclass (matches RenderConfigPanel / PropertyEditor pattern)
 * - QVBoxLayout → QScrollArea → container widget with vertical row list
 * - Clicking a row's name emits objectSelected(int) via Qt signal
 * - Visibility toggles emit visibilityChanged(int, bool, bool)
 * - No Vulkan or Renderer includes — pure Qt6 Widgets
 */

#pragma once

#include "UIPanel.h"

class QGroupBox;
class QScrollArea;
class QVBoxLayout;

namespace neurus
{

class Outliner : public UIPanel
{
	Q_OBJECT

public:
	static constexpr PanelType kType = PanelType::Outliner;

	explicit Outliner(QWidget* parent = nullptr);
	~Outliner() override = default;

	Outliner(const Outliner&) = delete;
	Outliner& operator=(const Outliner&) = delete;

	/**
	 * @brief Per-frame refresh from UIContext.
	 *
	 * Currently a no-op — hard-coded demo data does not change per-frame.
	 * Future: rebuild rows from scene object pools carried by UIContext.
	 *
	 * @param ctx Read-only UI context (unused in Phase 1).
	 */
	void Refresh(const UIContext& ctx) override;

signals:
	/** @brief Emitted when a user clicks on a scene object row in the outliner. */
	void objectSelected(int objectId);

	/**
	 * @brief Emitted when visibility toggles change for an object.
	 * @param objectId       Unique object identifier.
	 * @param viewportVisible True if viewport visibility is enabled.
	 * @param renderVisible   True if render visibility is enabled.
	 */
	void visibilityChanged(int objectId, bool viewportVisible, bool renderVisible);

private:
	/**
	 * @brief Builds the scrollable vertical list UI with hard-coded demo rows.
	 *
	 * Follows RenderConfigPanel constructor pattern:
	 * QVBoxLayout → QScrollArea → container widget → QVBoxLayout → rows.
	 */
	void BuildUI();

	/**
	 * @brief Creates a collapsible QGroupBox for a category of objects.
	 *
	 * Matches the RenderConfigPanel Build*Section() pattern: creates a
	 * QGroupBox, adds it to m_listLayout, and returns the group so the
	 * caller can add rows to a new QVBoxLayout inside it.
	 *
	 * @param title Category header text (e.g. "Cameras").
	 * @return The QGroupBox widget (caller uses group->layout() to add rows).
	 */
	QGroupBox* AddCategoryGroup(const QString& title);

	/**
	 * @brief Creates a single row widget matching Blender's Outliner design.
	 *
	 * Row layout (all in QHBoxLayout, 28px height):
	 *   [colored type label 22x22] [name QPushButton (stretching)] [eye toggle] [monitor toggle]
	 *
	 * The object ID is stored via QWidget::setProperty("objectId", id)
	 * and is never displayed to the user.
	 *
	 * @param typeLetter Single character for the type icon ("C", "L", "M").
	 * @param typeColor  CSS background color for the type icon.
	 * @param name       Display name shown in the row.
	 * @param objectId   Unique object identifier (stored, not shown).
	 * @param rowIndex   Row position in the list (0-based). Used for alternating background.
	 * @return A QWidget row ready to be added to the vertical list layout.
	 */
	QWidget* CreateRow(const QString& typeLetter, const QString& typeColor,
	                   const QString& name, int objectId, int rowIndex);

	QScrollArea* m_scrollArea = nullptr;
	QWidget*     m_container  = nullptr;
	QVBoxLayout* m_listLayout = nullptr;
};

} // namespace neurus
