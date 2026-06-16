/**
 * @file Log.h
 * @brief Debug logging utility.
 *
 * Provides:
 *   NEURUS_LOG - debug-only info logging (compiled out in Release).
 *   NEURUS_ERR - always-on error logging (prints in all builds).
 *
 * Both macros automatically inject __func__ and __LINE__ for traceability.
 *
 * Usage:
 *   NEURUS_LOG("[Swapchain] " << extent.width << "x" << extent.height);
 *   NEURUS_ERR("[Texture] failed: " << reason);
 */

#pragma once

#include <iostream>

/**
 * @brief Debug-only info log.
 *
 * Prints to std::cout with function name and line number prefix.
 * Compiled out entirely in Release builds.
 */
#ifdef _DEBUG
#define NEURUS_LOG(msg) \
	std::cout << "[" << __func__ << ":" << __LINE__ << "] " << msg << "\n"
#else
#define NEURUS_LOG(msg) ((void)0)
#endif

/**
 * @brief Always-on error log.
 *
 * Prints to std::cerr with function name and line number prefix.
 * Active in all build configurations so errors are never silently swallowed.
 */
#define NEURUS_ERR(msg) \
	std::cerr << "[" << __func__ << ":" << __LINE__ << "] ERROR: " << msg << "\n"
