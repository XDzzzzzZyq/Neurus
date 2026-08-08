/**
 * @file AssetPath.cpp
 * @brief Asset path resolution (see AssetPath.h).
 */

#include "asset/data/AssetPath.h"

#include <filesystem>

namespace neurus
{

namespace
{

/** @brief True for Windows drive (C:) or absolute root (/ or \) paths. */
bool IsAbsolutePath(const std::string& path)
{
	return (!path.empty() && (path[0] == '/' || path[0] == '\\'))
		|| (path.size() > 1 && path[1] == ':');
}

} // anonymous namespace

std::string ResolveAssetPath(const std::string& path)
{
	if (path.empty() || IsAbsolutePath(path))
		return path;

	// Strip a leading "res/" prefix and resolve against the compile-time res
	// dir (mirrors the shader resolver's NEURUS_SHADER_DIR convention), so
	// "res/obj/sphere.obj" -> NEURUS_RES_DIR + "obj/sphere.obj".
	if (path.rfind("res/", 0) == 0)
	{
		std::string resolved = std::string(NEURUS_RES_DIR) + path.substr(4);
		if (std::filesystem::exists(resolved))
			return resolved;
	}

	// Fallback: working-directory-relative with up-walks (tests run from
	// build/ or build/debug/test/, where res/ is copied).
	for (const auto& prefix : {"", "../", "../../", "../../../"})
	{
		std::string resolved = std::string(prefix) + path;
		if (std::filesystem::exists(resolved))
			return resolved;
	}

	return path; // not found - caller logs and stays an identity shell
}

} // namespace neurus
