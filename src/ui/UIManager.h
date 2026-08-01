#pragma once

#include <QMainWindow>
#include "platform/PlatformSurface.h"
#include <map>
#include <string>

#include "panels/UIPanel.h"

namespace ads {
class CDockManager;
class CDockWidget;
}

namespace neurus {

class UIManager : public QMainWindow
{
	Q_OBJECT

public:
	explicit UIManager(QWidget* parent = nullptr);
	~UIManager() override;

	/** @brief Returns the Viewport's native window handle for VkSurface creation. */
	NativeWindowHandle getViewportHwnd() const;

	/** @brief Returns viewport widget width in pixels. */
	int getViewportWidth() const;

	/** @brief Returns viewport widget height in pixels. */
	int getViewportHeight() const;

	/**
	 * @brief Refreshes all UIPanel subclasses with the current UIContext.
	 *
	 * Iterates over all registered panels and calls Refresh(ctx) on each.
	 * Called from the newFrame loop after DrawFrame() to keep UI widgets
	 * in sync with Editor/Project state.
	 *
	 * @param ctx Read-only UI context carrying Editor/Project state.
	 */
	void Refresh(const UIContext& ctx);

	/**
	 * @brief Returns typed pointer to a panel by its type.
	 *
	 * Each panel class must provide static constexpr PanelType kType
	 * (e.g. Viewport::kType == PanelType::Viewport).
	 *
	 * @tparam PanelClass UIPanel subclass (Viewport, Outliner, etc.)
	 * @return Non-owning pointer to the panel, or nullptr if not found.
	 */
	template<typename PanelClass>
	PanelClass* GetPanel() const
	{
		auto it = m_panels.find(PanelClass::kType);
		if (it != m_panels.end())
			return qobject_cast<PanelClass*>(it->second);
		return nullptr;
	}

	/**
	 * @brief Returns a dock widget by its PanelType.
	 * @param type PanelType enum value.
	 * @return Non-owning pointer, or nullptr if not found.
	 */
	ads::CDockWidget* GetDock(PanelType type) const
	{
		auto it = m_docks.find(type);
		return (it != m_docks.end()) ? it->second : nullptr;
	}

	/**
	 * @brief Serializes window geometry + ADS dock state into an opaque blob.
	 *
	 * The returned string bundles base64(window geometry) and base64(dock
	 * state) separated by a newline. Application owns persistence.
	 */
	std::string ExportLayout() const;

	/**
	 * @brief Restores window geometry + ADS dock state from a blob produced
	 *        by ExportLayout(). No-op if blob is empty or malformed.
	 */
	void ApplyLayout(const std::string& blob);

private:
	void CreateMenus();
	void CreateDocks();
	void RestoreDefaultLayout();

	ads::CDockManager* win_dockManager = nullptr;

	// --- Panel registry (raw pointers; Qt parent-child manages lifetime) ---
	std::map<PanelType, QWidget*> m_panels;

	// --- Dock registry (raw pointers; CDockManager owns via Qt parent-child) ---
	std::map<PanelType, ads::CDockWidget*> m_docks;
};

} // namespace neurus
