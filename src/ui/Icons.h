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

	static void Initialize();

	/**
	 * @brief Returns the QIcon for the given name ("folder:name" format).
	 */
	static const QIcon& GetIcon(const std::string& name);

	/**
	 * @brief Returns the type icon for a scene object GOType.
	 *
	 * GOType int values: 1=MESH, 3=LIGHT, 4=CAMERA, others=MESH fallback.
	 */
	static QIcon ObjectIcon(int goType);

private:
	static void RegisterIcon(const std::string& name, const QString& resourcePath);

	static std::unordered_map<std::string, QString> s_paths;
	static std::unordered_map<std::string, QIcon>   s_cache;
	static bool                                     s_initialized;
};

} // namespace neurus
