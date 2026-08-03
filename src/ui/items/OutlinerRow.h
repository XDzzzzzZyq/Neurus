/**
 * @file OutlinerRow.h
 * @brief Single row widget for the Outliner panel (pool-friendly).
 *
 * OutlinerRow encapsulates a Blender-style outliner row: [type icon] [name]
 * [eye toggle] [monitor toggle]. It is self-contained with no scene-layer
 * dependencies — the object pointer is stored directly and surfaced through
 * Qt signals.
 *
 * Architecture:
 * - Constructor creates the layout and child widgets once.
 * - setObject() binds object data (icon name, name, object*); resets toggles.
 * - setSelectionMode() / setRowIndex() apply visual state (QSS property + alternating bg).
 * - Signal lambdas read from m_object at emission time, so recycling
 *   a row to a new object is transparent — no manual rewire needed.
 * - Visibility toggle buttons swap between visible/invisible icons
 *   loaded via Icons::GetIcon().
 */

#pragma once

#include <QIcon>
#include <QWidget>

#include "editor/events/SceneEvents.h"
#include "scene/UID.h"

#include <cstdint>

#include <string>

class QLabel;
class QPushButton;

namespace neurus
{

/** @brief Forward declaration (defined in core/Selections.h). */
enum class SelectionMode : uint32_t;

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
	 * Creates the layout and child widgets once. Call setObject() to
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
	 * Updates the type icon pixmap, name text, and stored object pointer.
	 * Resets visibility toggles to checked (signals blocked to avoid
	 * cascading events during pool recycling) and sets their icons.
	 *
	 * @param icon     Type icon for this object.
	 * @param name     Display name shown in the row.
	 * @param object   Scene object pointer (non-owning).
	 */
	void setObject(const QIcon& icon, const QString& name, const ObjectID* object);

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
	void setVisibilities(bool viewportVisible, bool renderVisible);

	/**
	 * @brief Sets selection visual state via QSS dynamic property.
	 *
	 * Maps SelectionMode to "selectionState" property on m_nameBtn:
	 *   Active   → "active"   (#ff6f00 via QSS)
	 *   Selected → "selected" (#4A90D9 via QSS)
	 *   Neither  → "normal"   (#000000 via QSS)
	 *
	 * @param mode   Selection mode bitmask (Active / Selected).
	 */
	void setSelectionMode(SelectionMode mode);

	/**
	 * @brief Sets row index for alternating background.
	 *
	 * Odd rows get a translucent white background, even rows are transparent.
	 * No-op if rowIndex hasn't changed (dirty check).
	 *
	 * @param rowIndex  0-based row position.
	 */
	void setRowIndex(int rowIndex);

	/** @brief Returns the bound object pointer (nullptr if none). */
	const ObjectID* getObject() const { return m_object; }

signals:
	/** @brief Emitted when the name button is clicked. */
	void objectSelected(const ObjectSelected& e);

	/** @brief Emitted when either visibility toggle changes. */
	void visibilityChanged(const VisibilityChanged& e);

private:
	/** @brief Sets the eye button colorize effect from current checked state. */
	void setEyeBtnColor();

	/** @brief Sets the render button colorize effect from current checked state. */
	void setRenderBtnColor();

	QLabel*      m_typeLabel     = nullptr;
	QPushButton* m_nameBtn       = nullptr;
	QPushButton* m_eyeBtn        = nullptr;
	QPushButton* m_renderBtn     = nullptr;
	const ObjectID* m_object = nullptr;
	int 		 m_mode          = -1;
	int 		 m_idx           = -1;
	bool         m_eyeVisible    = true;
	bool         m_renderVisible = true;
};

} // namespace neurus
