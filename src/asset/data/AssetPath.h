/**
 * @file AssetPath.h
 * @brief Asset path resolution helper (asset layer).
 *
 * Resolves relative "res/..." resource paths to an existing file on disk:
 * 1. Absolute paths pass through unchanged.
 * 2. "res/..." paths resolve against the compile-time NEURUS_RES_DIR
 *    (${CMAKE_SOURCE_DIR}/res), mirroring the renderer's NEURUS_SHADER_DIR
 *    convention for shaders - so the app works regardless of the working dir.
 * 3. Fallback: working-directory-relative with a few up-walks (tests).
 *
 * Paths stored in project files stay RELATIVE ("res/obj/sphere.obj"), so
 * .neurus.json files never contain absolute paths.
 */

#pragma once

#include <string>

namespace neurus
{

/**
 * @brief Resolves a resource path to an existing file.
 * @param path "res/..." relative path, CWD-relative path, or absolute path.
 * @return The first existing candidate, or @p path unchanged if none found
 *         (the caller logs the failure and stays an identity shell).
 */
std::string ResolveAssetPath(const std::string& path);

} // namespace neurus
