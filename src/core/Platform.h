#pragma once

/**
 * @file Platform.h
 * @brief Cross-platform type definitions for native window handles.
 */

#ifdef _WIN32
#include <Windows.h>
#else
// On macOS/Linux, HWND and HINSTANCE are opaque pointers.
// macOS: HWND holds the NSView*/CAMetalLayer* via QWidget::winId().
using HWND = void*;
using HINSTANCE = void*;
#endif
