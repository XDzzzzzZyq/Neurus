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

#ifdef _WIN32
#include <io.h>
#define NEURUS_ISATTY(fd) _isatty(_fileno(fd))
#else
#include <unistd.h>
#define NEURUS_ISATTY(fd) isatty(fileno(fd))
#endif

/**
 * @brief Debug-only info log.
 *
 * Prints to std::cout with function name and line number prefix.
 * Compiled out entirely in Release builds.
 *
 * Debug detection: MSVC's CRT auto-defines _DEBUG in Debug builds. Clang/GCC
 * do not; they rely on the standard NDEBUG macro (absent in Debug). Accept
 * either signal so the log is active in Debug on every supported compiler.
 */
#if defined(_DEBUG) || !defined(NDEBUG)
#define NEURUS_LOG(msg) \
	do { \
		if (NEURUS_ISATTY(stdout)) { \
			std::cout << "\033[36m[" << __func__ << ":" << __LINE__ << "]\033[0m " << msg << "\n"; \
		} else { \
			std::cout << "[" << __func__ << ":" << __LINE__ << "] " << msg << "\n"; \
		} \
	} while(0)
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
	do { \
		if (NEURUS_ISATTY(stderr)) { \
			std::cerr << "\033[1;31m[" << __func__ << ":" << __LINE__ << "] ERROR:\033[0m " << msg << "\n"; \
		} else { \
			std::cerr << "[" << __func__ << ":" << __LINE__ << "] ERROR: " << msg << "\n"; \
		} \
	} while(0)
