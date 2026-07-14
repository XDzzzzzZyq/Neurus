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
 * - SetObject() reconfigures an existing row for a different scene object.
 * - Signal lambdas read from m_objectId at emission time, so recycling
 *   a row to a new objectId is transparent — no manual rewire needed.
 */

#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace neurus
{

/**
 * @brief A single row in the Outliner list view (pool-compatible).
 *
 * Layout (all in QHBoxLayout, 28px height):
 *   [colored type label 22x22] [name QPushButton (stretching)] [eye toggle] [monitor toggle]
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

	/**
	 * @brief Configures (or reconfigures) this row for a given scene object.
	 *
	 * Updates the type icon, name, alternating background, and stored
	 * objectId. Signal lambdas read m_objectId at emission time, so
	 * no manual disconnect/reconnect is needed when recycling a row
	 * to a different object.
	 *
	 * @param typeLetter Single character type icon ("C", "L", "M", etc.).
	 * @param typeColor  CSS background color for the type icon.
	 * @param name       Display name shown in the row.
	 * @param objectId   Unique object identifier.
	 * @param rowIndex   0-based row position for alternating background.
	 */
	void SetObject(const QString& typeLetter, const QString& typeColor,
	               const QString& name, int objectId, int rowIndex);

	/** @brief Returns the stored object identifier. */
	int GetObjectId() const { return m_objectId; }

signals:
	/** @brief Emitted when the name button is clicked. */
	void objectSelected(int objectId);

	/**
	 * @brief Emitted when either visibility toggle changes.
	 * @param objectId       Unique object identifier.
	 * @param viewportVisible True if viewport visibility is enabled.
	 * @param renderVisible   True if render visibility is enabled.
	 */
	void visibilityChanged(int objectId, bool viewportVisible, bool renderVisible);

private:
	QLabel*      m_typeLabel  = nullptr;
	QPushButton* m_nameBtn    = nullptr;
	QPushButton* m_eyeBtn     = nullptr;
	QPushButton* m_renderBtn  = nullptr;
	int m_objectId = 0;
};

} // namespace neurus
