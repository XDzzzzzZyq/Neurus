/**
 * @file UIPanel.h
 * @brief Base class for all dock panel widgets in the UI layer.
 *
 * UIPanel provides the common QWidget base for dockable panels (Outliner,
 * PropertyPanel, RenderConfigPanel). All panels inherit UIPanel so the
 * dock system can treat them uniformly.
 *
 * Architecture:
 * - Inherits QWidget — panels are embeddable in ads::CDockWidget
 * - Q_OBJECT macro for MOC signal/slot generation
 * - Each panel has a PanelType enum value for O(1) lookup in the panel registry
 * - PanelName() provides the human-readable dock title
 *
 * @note UI Layer — no Vulkan or Renderer dependencies.
 */

#pragma once

#include <QString>
#include <QWidget>
#include <cstdint>

#include "UIContext.h"

namespace neurus
{

/** @brief Enum identifying each dock panel type for registry lookup. */
enum class PanelType : uint8_t
{
	Viewport,
	Outliner,
	PropertyPanel,
	RenderConfig,
	ShaderEditor,
	Profiling,
	Log,
	Count
};

class UIPanel : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs a UIPanel.
	 * @param type PanelType enum identifying this panel's role.
	 * @param name Human-readable dock title. If empty, a default is derived from @p type.
	 * @param parent Parent widget.
	 */
	explicit UIPanel(PanelType type, const QString& name = QString(), QWidget* parent = nullptr)
		: QWidget(parent)
		, m_type(type)
		, m_name(name.isEmpty() ? DefaultName(type) : name) {}
	~UIPanel() override = default;

	UIPanel(const UIPanel&) = delete;
	UIPanel& operator=(const UIPanel&) = delete;

	/** @brief Returns the panel type enum. */
	PanelType GetPanelType() const { return m_type; }

	/** @brief Returns the human-readable panel name (e.g. "Viewport", "Outliner"). */
	const QString& PanelName() const { return m_name; }

	/**
	 * @brief Refreshes the panel's display from a UIContext snapshot.
	 *
	 * Called by UIManager::Refresh() each frame. Each subclass implements
	 * this to extract the data it needs from the context (RenderConfig,
	 * Scene, etc.) and update its widgets.
	 *
	 * @param ctx Read-only UI context carrying Editor/Project state.
	 */
	virtual void Refresh(const neurus::UIContext& ctx) = 0;

private:
	/** @brief Returns the default display name for a given PanelType. */
	static QString DefaultName(PanelType type)
	{
		switch (type)
		{
		case PanelType::Viewport:       return "Viewport";
		case PanelType::Outliner:       return "Outliner";
		case PanelType::PropertyPanel: return "Property Panel";
		case PanelType::RenderConfig:   return "Render Config";
		case PanelType::ShaderEditor:   return "Shader Editor";
		case PanelType::Profiling:      return "Profiling";
		case PanelType::Log:            return "Log";
		default:                        return "Unknown";
		}
	}

	PanelType m_type;
	QString   m_name;
};

} // namespace neurus
