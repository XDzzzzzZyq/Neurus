/**
 * @file Icons.h
 * @brief Lazy-loading SVG icon library for the UI layer.
 *
 * Icons maintains a hardcoded registry of icon name→resource path mappings
 * and an on-demand QIcon cache. The first call to GetIcon() for a given
 * name loads the SVG from the Qt resource system; subsequent calls return
 * the cached QIcon directly.
 *
 * Naming convention: "folder:name" (e.g. "scene:mesh" → scene/mesh.svg).
 *
 * Architecture:
 * - Fully static class — no instantiation needed.
 * - Initialize() populates the path registry; called once by UIManager.
 * - GetIcon() lazily loads and caches QIcon objects process-wide.
 * - Lives in src/ui/ — pure UI layer, no Vulkan or Renderer dependencies.
 */

#pragma once

#include <QIcon>
#include <QString>
#include <string>
#include <unordered_map>

namespace neurus
{

class Icons
{
public:
	Icons() = delete;

	/**
	 * @brief Populates the hardcoded icon name→resource path registry.
	 *
	 * Must be called once before any GetIcon() calls. Safe to call
	 * multiple times (idempotent). Typically invoked by UIManager
	 * during construction.
	 */
	static void Initialize();

	/**
	 * @brief Returns the QIcon for the given name, loading it on first access.
	 *
	 * Looks up @p name in the path registry populated by Initialize().
	 * On first call, loads the SVG from the Qt resource system and caches
	 * the resulting QIcon. Subsequent calls return the cached instance.
	 *
	 * @param name Icon name in "folder:name" format (e.g. "scene:mesh").
	 * @return Const reference to the cached QIcon. If @p name is not
	 *         registered, returns a default-constructed (empty) QIcon.
	 */
	static const QIcon& GetIcon(const std::string& name);

private:
	static void RegisterIcon(const std::string& name, const QString& resourcePath);

	static std::unordered_map<std::string, QString> s_paths;
	static std::unordered_map<std::string, QIcon>   s_cache;
	static bool                                     s_initialized;
};

} // namespace neurus
