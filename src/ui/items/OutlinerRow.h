/**
 * @file OutlinerRow.h
 * @brief Single row widget for the Outliner panel (pool-friendly).
 *
 * OutlinerRow encapsulates a Blender-style outliner row: [type icon] [name]
 * [eye toggle] [monitor toggle]. It is self-contained with no scene-layer
 * dependencies — the object ID is stored as a plain int and surfaced through
 * Qt signals.
 *
 * Architecture:
 * - Constructor creates the layout and child widgets once.
 * - SetObject() binds object data (icon name, name, id); resets toggles.
 * - SetStyle() applies visual state (selection highlight, alternating bg).
 * - Signal lambdas read from m_objectId at emission time, so recycling
 *   a row to a new objectId is transparent — no manual rewire needed.
 * - Visibility toggle buttons swap between visible/invisible icons
 *   loaded via Icons::GetIcon().
 */

#pragma once

#include <QIcon>
#include <QWidget>

#include "editor/events/EditorEvents.h"

#include <string>

class QLabel;
class QPushButton;

namespace neurus
{

/**
 * @brief A single row in the Outliner list view (pool-compatible).
 *
 * Layout (all in QHBoxLayout, 28px height):
 *   [type icon 22x22] [name QPushButton (stretching)] [eye toggle] [monitor toggle]
 */
class OutlinerRow : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs an empty outliner row (no object bound yet).
	 * @param parent Parent widget.
	 *
	 * Creates the layout and child widgets once. Call SetObject() to
	 * bind a scene object to this row.
	 */
	explicit OutlinerRow(QWidget* parent = nullptr);
	~OutlinerRow() override = default;

	OutlinerRow(const OutlinerRow&) = delete;
	OutlinerRow& operator=(const OutlinerRow&) = delete;

	// -------------------------------------------------------------------
	// Two-phase row configuration: bind data, then apply style
	// -------------------------------------------------------------------

	/**
	 * @brief Binds a scene object's identity data to this row.
	 *
	 * Updates the type icon pixmap, name text, and stored objectId.
	 * Resets visibility toggles to checked (signals blocked to avoid
	 * cascading events during pool recycling) and sets their icons.
	 *
	 * @param icon     Type icon for this object.
	 * @param name     Display name shown in the row.
	 * @param objectId Unique object identifier.
	 */
	void SetObject(const QIcon& icon, const QString& name, int objectId);

	/**
	 * @brief Sets visibility toggle states without emitting signals.
	 *
	 * Updates both the viewport (eye) and render (monitor) toggle buttons
	 * using blockSignals so no false visibilityChanged emissions occur.
	 * Also swaps the button icons to reflect the new state.
	 *
	 * @param viewportVisible True to enable viewport visibility.
	 * @param renderVisible   True to enable render visibility.
	 */
	void SetVisibilities(bool viewportVisible, bool renderVisible);

	/**
	 * @brief Applies visual styling: selection highlight and row background.
	 *
	 * Active  → orange name text (#ff6f00).
	 * Selected (non-active) → blue name text (#4A90D9).
	 * Neither → black name text (#000000).
	 * Row background alternates on odd/even rowIndex for readability.
	 *
	 * @param mode      Bitmask representing selection state (Active, Selected).
	 * @param rowIndex  0-based row position for alternating background.
	 */
	void SetStyle(int mode, int rowIndex);

	/** @brief Returns the stored object identifier. */
	int GetObjectId() const { return m_objectId; }

signals:
	/** @brief Emitted when the name button is clicked. */
	void objectSelected(const ObjectSelected& e);

	/** @brief Emitted when either visibility toggle changes. */
	void visibilityChanged(const VisibilityChanged& e);

private:
	/** @brief Updates both visibility toggle icons from the Icons cache. */
	void UpdateToggleIcons();

	QLabel*      m_typeLabel  = nullptr;
	QPushButton* m_nameBtn    = nullptr;
	QPushButton* m_eyeBtn     = nullptr;
	QPushButton* m_renderBtn  = nullptr;
	int          m_objectId   = -1;
	int 		 m_mode       = -1;
	int 		 m_idx		  = -1;
};

} // namespace neurus
