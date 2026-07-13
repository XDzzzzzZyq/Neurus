#pragma once

#include <QMainWindow>
#include <Windows.h>
#include <map>

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

	/** @brief Returns the Viewport's native HWND for VkSurface creation. */
	HWND getViewportHwnd() const;

	/** @brief Returns viewport widget width in pixels. */
	int getViewportWidth() const;

	/** @brief Returns viewport widget height in pixels. */
	int getViewportHeight() const;

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

private:
	void CreateMenus();
	void CreateDocks();
	void SaveLayout();
	void LoadLayout();
	void RestoreDefaultLayout();

	ads::CDockManager* win_dockManager = nullptr;

	// --- Panel registry (raw pointers; Qt parent-child manages lifetime) ---
	std::map<PanelType, QWidget*> m_panels;

	// --- Dock registry (raw pointers; CDockManager owns via Qt parent-child) ---
	std::map<PanelType, ads::CDockWidget*> m_docks;
};

} // namespace neurus
