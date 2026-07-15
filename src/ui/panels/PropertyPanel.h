#pragma once

#include "UIPanel.h"
#include "editor/events/EditorEvents.h"
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace neurus {

/**
 * @brief Transform-only property panel for editing position/rotation/scale of a selected object.
 *
 * Displays an editable QGroupBox with color-coded XYZ spinboxes for position,
 * rotation (Euler degrees), and scale. Emits transformChanged() on every edit.
 * Call LoadTransform() to populate from external state; ClearTransform() to
 * disable when nothing is selected.
 *
 * @note Pure UI layer — no Scene, Camera, or Light dependencies.
 * @note Owned by UIManager as a right dock widget.
 */
class PropertyPanel : public UIPanel
{
	Q_OBJECT

public:
	static constexpr PanelType kType = PanelType::PropertyPanel;

	explicit PropertyPanel(QWidget* parent = nullptr);
	~PropertyPanel() override = default;

	PropertyPanel(const PropertyPanel&) = delete;
	PropertyPanel& operator=(const PropertyPanel&) = delete;

	void Refresh(const UIContext& ctx) override;

	/**
	 * @brief Populates the transform editor from raw float values and enables editing.
	 * @param objectId Currently selected object ID (carried back in transformChanged events).
	 * @param posX/Y/Z  World-space position.
	 * @param rotX/Y/Z  Euler rotation in degrees (pitch=X, roll=Y, yaw=Z).
	 * @param sclX/Y/Z  Per-axis scale.
	 */
	void LoadTransform(int objectId,
	                   float posX, float posY, float posZ,
	                   float rotX, float rotY, float rotZ,
	                   float sclX, float sclY, float sclZ);

	/** @brief Disables the transform editor and clears the tracked object ID. */
	void ClearTransform();

signals:
	/** @brief Emitted when any transform field is edited by the user. */
	void transformChanged(const TransformChanged& e);

private:
	void BuildTransformEditor();

	/** @brief Enables or disables the entire transform editor group box. */
	void SetEnabled(bool enabled);

	/** @brief Reads current transform editor values into a TransformChanged event. */
	TransformChanged CollectTransform() const;

	int    m_currentObjectId = -1;

	QVBoxLayout*    m_mainLayout = nullptr;
	QGroupBox*      m_transformGroup = nullptr;
	QDoubleSpinBox* m_posX = nullptr;
	QDoubleSpinBox* m_posY = nullptr;
	QDoubleSpinBox* m_posZ = nullptr;
	QDoubleSpinBox* m_rotX = nullptr;
	QDoubleSpinBox* m_rotY = nullptr;
	QDoubleSpinBox* m_rotZ = nullptr;
	QDoubleSpinBox* m_sclX = nullptr;
	QDoubleSpinBox* m_sclY = nullptr;
	QDoubleSpinBox* m_sclZ = nullptr;
	QPushButton*    m_resetBtn = nullptr;
};

} // namespace neurus
