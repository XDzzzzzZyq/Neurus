# Platform Surface Factory — Design Spec

**Date:** 2026-07-24  
**Status:** Approved  
**Scope:** Extract platform-specific Vulkan surface creation into a virtual interface with per-platform implementations in subfolders.

---

## Problem

`VulkanContext.cpp` and `Application.cpp` contain `#ifdef _WIN32` / `#elif __APPLE__` blocks for:
- Instance extension selection
- Instance create flags (portability enumeration)
- Surface creation (`Win32SurfaceCreateInfoKHR` vs `MetalSurfaceCreateInfoEXT`)
- Native handle setup (CAMetalLayer on macOS)

Adding a third platform (Linux/X11 or Wayland) would require editing these files and adding more branches. The `Platform.h` type aliases (`using HWND = void*`) are a fragile workaround.

## Solution

A `PlatformSurface` abstract interface with one subclass per platform (~40 lines each). Follows Godot's `RenderingContextDriverVulkan` pattern.

---

## File Layout

```
src/platform/
├── CMakeLists.txt              # neurus_platform library, conditionally adds subfolder
├── PlatformSurface.h           # Abstract interface + NativeWindowHandle typedef
├── PlatformSurface.cpp         # Compile-time factory: CreatePlatformSurface()
├── win/
│   ├── Win32Surface.h
│   └── Win32Surface.cpp        # ~35 lines
└── mac/
    ├── MacOSSurface.h
    └── MacOSSurface.mm          # ~80 lines (includes NeurusMetalView ObjC code)
```

---

## Interface

```cpp
// src/platform/PlatformSurface.h
#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <memory>

namespace neurus {

/// Opaque native window handle. HWND on Win32, NSView* on macOS.
using NativeWindowHandle = void*;

/// Abstract platform surface — isolates Vulkan surface creation from platform details.
class PlatformSurface {
public:
    virtual ~PlatformSurface() = default;

    /// Vulkan instance extensions required by this platform's surface type.
    virtual std::vector<const char*> requiredInstanceExtensions() const = 0;

    /// Instance create flags (e.g., eEnumeratePortabilityKHR on macOS). Default: none.
    virtual vk::InstanceCreateFlags instanceCreateFlags() const { return {}; }

    /// Create a VkSurfaceKHR from a native window handle.
    /// On macOS this internally configures the CAMetalLayer.
    virtual vk::raii::SurfaceKHR createSurface(
        const vk::raii::Instance& instance,
        NativeWindowHandle windowHandle) const = 0;

    /// Notify the platform layer of a resize (e.g., update CAMetalLayer drawable size).
    /// Default: no-op (Win32 doesn't need this — swapchain recreation handles it).
    virtual void onResize(NativeWindowHandle windowHandle,
                          uint32_t width, uint32_t height) const {}
};

/// Compile-time factory — returns the platform-appropriate PlatformSurface.
std::unique_ptr<PlatformSurface> CreatePlatformSurface();

} // namespace neurus
```

---

## Platform Implementations

### Win32Surface (win/Win32Surface.cpp)

```cpp
requiredInstanceExtensions() → { VK_KHR_SURFACE_EXTENSION_NAME,
                                   VK_KHR_WIN32_SURFACE_EXTENSION_NAME }
instanceCreateFlags()        → {} (default)
createSurface(instance, h)   → GetModuleHandle(nullptr) + Win32SurfaceCreateInfoKHR
onResize(...)                → no-op (default)
```

Includes `<Windows.h>` — only file in the project that does.

### MacOSSurface (mac/MacOSSurface.mm)

```cpp
requiredInstanceExtensions() → { VK_KHR_SURFACE_EXTENSION_NAME,
                                   VK_EXT_METAL_SURFACE_EXTENSION_NAME,
                                   VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME }
instanceCreateFlags()        → vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR
createSurface(instance, h)   → makeViewMetalCompatible(h) + MetalSurfaceCreateInfoEXT
onResize(h, w, h)            → updateMetalLayerSize(h)
```

Contains the `NeurusMetalView` Objective-C class (moved from `src/ui/MacMetalLayer.mm`).

---

## Factory (PlatformSurface.cpp)

```cpp
#include "PlatformSurface.h"

#ifdef _WIN32
#include "win/Win32Surface.h"
#elif __APPLE__
#include "mac/MacOSSurface.h"
#endif

namespace neurus {

std::unique_ptr<PlatformSurface> CreatePlatformSurface() {
#ifdef _WIN32
    return std::make_unique<Win32Surface>();
#elif __APPLE__
    return std::make_unique<MacOSSurface>();
#else
    static_assert(false, "No PlatformSurface implementation for this platform");
#endif
}

} // namespace neurus
```

---

## Integration Changes

### VulkanContext.cpp

**Before:**
```cpp
static std::vector<const char*> getRequiredInstanceExtensions() {
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif __APPLE__
    extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
    return extensions;
}
```

**After:**
```cpp
// VulkanContext::CreateInstance now takes PlatformSurface reference:
vk::raii::Instance VulkanContext::CreateInstance(const PlatformSurface& platform) {
    auto extensions = platform.requiredInstanceExtensions();
    // ... validation layers ...
    vk::InstanceCreateInfo createInfo(platform.instanceCreateFlags(), &appInfo, {}, extensions);
    // ...
}
```

### Application.cpp

**Before:**
```cpp
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#include "../ui/MacMetalLayer.h"
#endif

// In InitVulkan():
#ifdef _WIN32
    HINSTANCE hinstance = GetModuleHandle(nullptr);
    vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo(...);
#elif __APPLE__
    void* metalLayer = makeViewMetalCompatible(hwnd);
    vk::MetalSurfaceCreateInfoEXT surfaceCreateInfo(...);
#endif
    app_surface = std::make_unique<vk::raii::SurfaceKHR>(instance, surfaceCreateInfo);
```

**After:**
```cpp
#include "platform/PlatformSurface.h"

// Member:
std::unique_ptr<PlatformSurface> app_platform;

// In InitVulkan():
app_platform = CreatePlatformSurface();
auto vkInstance = VulkanContext::CreateInstance(*app_platform);
// ...
app_surface = std::make_unique<vk::raii::SurfaceKHR>(
    app_platform->createSurface(app_vkContext->instance(), app_mainWindow->getViewportHwnd()));

// In ResizeViewport():
app_platform->onResize(app_mainWindow->getViewportHwnd(), width, height);
```

### Files Deleted

| File | Reason |
|------|--------|
| `src/core/Platform.h` | Replaced by `NativeWindowHandle` in `PlatformSurface.h` |
| `src/ui/MacMetalLayer.h` | Absorbed into `mac/MacOSSurface.mm` |
| `src/ui/MacMetalLayer.mm` | Absorbed into `mac/MacOSSurface.mm` |

### Files That Update Their Include

| File | Old include | New include |
|------|-------------|-------------|
| `src/ui/UIManager.h` | `#include "core/Platform.h"` | `#include "platform/PlatformSurface.h"` |
| `src/ui/panels/Viewport.h` | `#include "core/Platform.h"` | `#include "platform/PlatformSurface.h"` |

These files only need the `NativeWindowHandle` typedef.

---

## CMake Structure

```cmake
# src/platform/CMakeLists.txt
add_library(neurus_platform STATIC
    PlatformSurface.h
    PlatformSurface.cpp
)

target_link_libraries(neurus_platform PUBLIC Vulkan::Vulkan)
target_include_directories(neurus_platform PUBLIC ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_sources(neurus_platform PRIVATE win/Win32Surface.h win/Win32Surface.cpp)
elseif(APPLE)
    target_sources(neurus_platform PRIVATE mac/MacOSSurface.h mac/MacOSSurface.mm)
    target_link_libraries(neurus_platform PRIVATE
        "-framework AppKit"
        "-framework QuartzCore"
        "-framework Metal"
    )
endif()
```

`neurus_app` and `neurus_ui` link against `neurus_platform`.

---

## Layer Dependency

```
UI (UIManager, Viewport) ──uses──> NativeWindowHandle (from platform/PlatformSurface.h)
App (Application)        ──owns──> PlatformSurface instance
App                      ──calls─> VulkanContext::CreateInstance(platform)
App                      ──calls─> platform->createSurface(instance, hwnd)
App                      ──calls─> platform->onResize(hwnd, w, h)
```

No layer violation: Platform is a leaf dependency (only Vulkan headers). UI uses only the typedef. App orchestrates.

---

## Success Criteria

1. `Application.cpp` and `VulkanContext.cpp` contain zero `#ifdef _WIN32` / `#elif __APPLE__` for surface/extension logic.
2. `make app` succeeds on macOS with identical runtime behavior.
3. Adding a future Linux platform requires only a new `src/platform/linux/` folder + 1 file — no changes to existing code.
4. `Platform.h` and `MacMetalLayer.h/.mm` are deleted.
