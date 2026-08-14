/**
 * @file Preferences.h
 * @brief Application-layer user preferences persisted to ~/.neurus/preferences.json.
 *
 * Preferences are app-level (NOT project-level) settings, owned and managed
 * exclusively by the Application: the active UI language, the render-loop
 * target FPS, and — in the future — options such as CUDA enablement, theme,
 * or shortcut schemes. The UI layer never sees this type: the Application
 * seeds the Preferences dialog with plain values (language, target FPS,
 * file path) and receives change requests back through UIEvents signals.
 *
 * Architecture:
 * - Pure data struct + cereal JSON persistence (Qt-free apart from
 *   QDir::homePath() used for the default file location).
 * - Missing/corrupt files fall back to defaults (never throw).
 * - "auto" language means "follow the system UI language"; resolving it to a
 *   concrete code is the Application's job (it owns I18n, so this data type
 *   stays free of any UI dependency).
 */

#pragma once

#include <string>

namespace neurus {

struct Preferences
{
	/** @brief Active UI language code ("en", "zh_CN"). "auto" = system-detect. */
	std::string language = "auto";

	/** @brief Render-loop target FPS (0 = unlimited, run as fast as possible). */
	int targetFps = 60;

	/**
	 * @brief Absolute path of the preferences file (~/.neurus/preferences.json).
	 */
	static std::string DefaultPath();

	/**
	 * @brief Loads settings from @p path.
	 * @param path Filesystem path (see DefaultPath()).
	 * @return true on success; false when the file is missing or corrupt
	 *         (defaults are kept in that case).
	 */
	bool Load(const std::string& path);

	/**
	 * @brief Saves settings to @p path, creating the parent directory.
	 * @param path Filesystem path (see DefaultPath()).
	 * @return true on success, false on I/O failure.
	 */
	bool Save(const std::string& path) const;
};

} // namespace neurus
