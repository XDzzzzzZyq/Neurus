/**
 * @file Preferences.h
 * @brief App-scoped user preferences persisted to ~/.neurus/preferences.json.
 *
 * Preferences are application-level (not project-level) settings: the active
 * UI language, the render-loop target FPS, etc. The Application owns the
 * instance, loads it at startup before the window is built, and saves it on
 * every change and on exit.
 *
 * Architecture:
 * - Pure data struct + cereal JSON persistence; no Qt widgets, no editor or
 *   renderer state.
 * - Missing/corrupt files fall back to defaults (never throw).
 * - The "auto" language resolves to the detected system language at load.
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
	 * @return true on success, false on I/O or parse failure.
	 */
	bool Save(const std::string& path) const;
};

} // namespace neurus
