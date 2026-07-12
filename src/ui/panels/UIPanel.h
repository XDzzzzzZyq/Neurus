/**
 * @file UIPanel.h
 * @brief Base class for all dock panel widgets in the UI layer.
 *
 * UIPanel provides the common QWidget base for dockable panels (Outliner,
 * PropertyEditor, RenderConfigPanel). All panels inherit UIPanel so the
 * dock system can treat them uniformly.
 *
 * Architecture:
 * - Inherits QWidget — panels are embeddable in ads::CDockWidget
 * - Q_OBJECT macro for MOC signal/slot generation
 * - Minimal base class; each panel defines its own layout and controls
 *
 * @note UI Layer — no Vulkan or Renderer dependencies.
 */

#pragma once

#include <QWidget>

namespace neurus
{

class UIPanel : public QWidget
{
	Q_OBJECT

public:
	explicit UIPanel(QWidget* parent = nullptr)
		: QWidget(parent) {}
	~UIPanel() override = default;

	UIPanel(const UIPanel&) = delete;
	UIPanel& operator=(const UIPanel&) = delete;
};

} // namespace neurus
