/**
 * @file Outliner.h
 * @brief Outliner dock panel — Blender-style vertical list view.
 *
 * Displays scene objects as a scrollable vertical list. Each row shows the
 * object type icon, name, and two visibility toggles (viewport / render).
 *
 * Architecture:
 * - UIPanel subclass (matches RenderConfigPanel / PropertyEditor pattern)
 * - QVBoxLayout → QScrollArea → container widget with vertical row list
 * - Rows are OutlinerRow widgets managed via a pool
 * - Pool grows as needed; extra rows are hidden (not destroyed)
 * - Each recycled row calls SetObject() — signal lambdas read the current
 *   m_objectId at emission time, so no manual rewire needed
 * - Row signals forwarded via Outliner::objectSelected / visibilityChanged
 * - No Vulkan, Renderer, or scene-layer headers — pure Qt6 Widgets
 */

#pragma once

#include "UIPanel.h"

#include "editor/events/EditorEvents.h"

#include <cstddef>
#include <vector>

class QGroupBox;
class QScrollArea;
class QVBoxLayout;

namespace neurus
{

class OutlinerRow;

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
	 * Uses a row pool: grows when scene objects exceed pool size, recycles
	 * existing rows via SetObject() otherwise. Extra rows are hidden.
	 *
	 * @param ctx Read-only UI context carrying Editor/Project state.
	 */
	void Refresh(const UIContext& ctx) override;

signals:
	/** @brief Emitted when a user clicks on a scene object row in the outliner. */
	void objectSelected(const ObjectSelected& e);

	/** @brief Emitted when visibility toggles change for an object. */
	void visibilityChanged(const VisibilityChanged& e);

private:
	/**
	 * @brief Creates a collapsible QGroupBox for a category of objects.
	 *
	 * @param title Category header text (e.g. "Cameras").
	 * @return The QGroupBox widget.
	 */
	QGroupBox* AddCategoryGroup(const QString& title);

	/**
	 * @brief Ensures the row pool has at least @p needed rows,
	 *        creating new ones as necessary.
	 */
	void EnsureRowPool(std::size_t needed);

	QScrollArea* m_scrollArea = nullptr;
	QWidget*     m_container  = nullptr;
	QVBoxLayout* m_listLayout = nullptr;

	/// Persistent "Scene" category group (created once).
	QGroupBox*     m_sceneGroup = nullptr;
	QVBoxLayout*   m_groupLayout = nullptr;

	/// Row pool — grows as needed, never shrinks.
	std::vector<OutlinerRow*> m_rowPool;
};

} // namespace neurus
