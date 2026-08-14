#pragma once

#include <QMainWindow>
#include <QStringList>
#include "platform/PlatformSurface.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "panels/UIPanel.h"

namespace ads {
class CDockManager;
class CDockWidget;
}

class QAction;
class QLabel;
class QMenu;

namespace neurus {

class Preferences;
class PreferencesDialog;

class UIManager : public QMainWindow
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs the main window.
	 * @param preferences App-scoped preferences (Application-owned, non-owning
	 *                    pointer) used by the Preferences dialog. Must outlive
	 *                    this window.
	 * @param parent Parent widget.
	 */
	explicit UIManager(Preferences* preferences, QWidget* parent = nullptr);
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

	/**
	 * @brief Re-applies every user-visible string in the active language:
	 *        menu bar, dock titles, panel texts, Preferences dialog.
	 *        Connected to I18n::languageChanged() and called once at startup.
	 */
	void RetranslateAll();

	/** @brief Shows (or creates) the Preferences dialog. */
	void OpenPreferences();

	/** @brief Populates the Undo submenu with the applied-operation stack. */
	void PopulateUndoMenu();
	/** @brief Populates the Redo submenu with the undone-operation stack. */
	void PopulateRedoMenu();

	ads::CDockManager* win_dockManager = nullptr;

	// --- Panel registry (raw pointers; Qt parent-child manages lifetime) ---
	std::map<PanelType, QWidget*> m_panels;

	// --- Dock registry (raw pointers; CDockManager owns via Qt parent-child) ---
	std::map<PanelType, ads::CDockWidget*> m_docks;

	// --- Menu translation registry: (action, i18n key) pairs ---
	std::vector<std::pair<QAction*, const char*>> m_menuItems;

	// --- Edit-menu Undo/Redo submenus (view-only stack lists) ---
	QMenu*                m_undoMenu = nullptr; ///< Expands to the applied-op stack.
	QMenu*                m_redoMenu = nullptr; ///< Expands to the undone-op stack.
	std::vector<QAction*> m_undoItems;          ///< Rows currently shown in Undo menu.
	std::vector<QAction*> m_redoItems;          ///< Rows currently shown in Redo menu.
	QStringList           m_undoLabels;         ///< Applied ops, oldest → newest.
	QStringList           m_redoLabels;         ///< Undone ops, in replay order.

	// --- Preferences dialog (lazy, non-dock panel) ---
	Preferences*      m_preferences = nullptr;      ///< Application-owned, non-owning.
	PreferencesDialog* m_preferencesDialog = nullptr; ///< Lazy-created; Qt parent owns.

	// --- Texture Viewer placeholder dock (not a UIPanel) ---
	ads::CDockWidget* m_textureDock  = nullptr;
	QLabel*           m_textureLabel = nullptr;
};

} // namespace neurus
