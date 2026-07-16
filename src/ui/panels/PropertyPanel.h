#pragma once

#include "UIPanel.h"
#include "editor/events/EditorEvents.h"
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace neurus {

class Vec3Spin;

/**
 * @brief Property Panel displaying type/name header and editable transform for the active
 *        scene object.
 *
 * Refresh() reads the active object from UIContext::scene→selections each frame,
 * displaying its o_name with type icon and populating the Position / Rotation / Scale
 * Vec3Spin widgets from its Transform3D. Editing any field emits a granular signal
 * (positionChanged / rotationChanged / scaleChanged).
 *
 * @note Pure UI layer — no scene mutation. Signals flow through ConnectUIEvent to Editor.
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

	/** @brief Reads active object from UIContext, updates header + transform editor. */
	void Refresh(const UIContext& ctx) override;

signals:
	void positionChanged(const PositionChanged& e);
	void rotationChanged(const RotationChanged& e);
	void scaleChanged(const ScaleChanged& e);

private:
	void BuildHeader();
	void BuildTransformEditor();

	/** @brief Enables or disables header + transform group. */
	void SetEnabled(bool enabled);

	// --- State ---
	int m_currentObjectId = -1;

	// --- Header ---
	QWidget* m_headerWidget = nullptr;
	QLabel*  m_iconLabel    = nullptr;
	QLabel*  m_nameLabel    = nullptr;
	QLabel*  m_emptyLabel   = nullptr;

	// --- Transform editor ---
	QGroupBox* m_transformGroup = nullptr;
	Vec3Spin*  m_posSpin        = nullptr;
	Vec3Spin*  m_rotSpin        = nullptr;
	Vec3Spin*  m_sclSpin        = nullptr;
};

} // namespace neurus
