# UI System

## Overview

The UI layer is a **Qt6 Widgets** application with **Qt-Advanced-Docking-System (ADS)** for dockable panels and **QVulkanWindow** for GPU rendering. It communicates with other layers exclusively through the EventBus (Qt Signals/Slots).

## Location

- `src/ui/NeurusMainWindow.h/cpp` — QMainWindow subclass with ADS dock manager + menus
- `src/ui/VulkanWindow.h/cpp` — QVulkanWindow subclass hosting the triangle renderer
- `src/ui/MainWindow.h/cpp` — (legacy) QWindow subclass
- `src/ui/VulkanWidget.h/cpp` — (legacy) QWidget subclass with native HWND for vk::raii surface
- `src/ui/qml/main.qml` — (legacy) QML source
- `src/render/QVulkanRenderer.h/cpp` — QVulkanWindowRenderer implementation (triangle pipeline)

## Rendering Architecture

### Primary Path: QVulkanWindow (C Vulkan API)

```
QVulkanInstance
  └── VulkanWindow : QVulkanWindow
        ├── Qt handles: instance, surface, device, swapchain, command buffer
        └── QVulkanRenderer : QVulkanWindowRenderer
              ├── initResources() → create pipeline + shader modules
              ├── startNextFrame() → vkCmdBeginRenderPass → draw(3) → endRenderPass
              │   → frameReady() → requestUpdate()
              └── releaseResources() → destroy pipeline, shader modules
```

- `QVulkanWindow` handles ALL infrastructure: instance, surface, device, swapchain (including recreation), command pool, primary command buffer
- `QVulkanRenderer` implements `QVulkanWindowRenderer` with C Vulkan API
- Pipeline uses `QVulkanWindow::defaultRenderPass()` (traditional render pass, not dynamic rendering)
- Continuous rendering via `requestUpdate()` in `startNextFrame()`

### Secondary Path: vk::raii + Dynamic Rendering (reference implementation)

```
VulkanWidget (QWidget with WA_NativeWindow)
  → vk::Win32SurfaceCreateInfoKHR → vk::raii::SurfaceKHR
  → VulkanContext → vk::raii::Device → Renderer → Swapchain → ShaderProgram
```

- `VulkanWidget` provides native HWND for Vulkan surface
- `Renderer::DrawFrame()` uses vk::raii fences/semaphores with `VK_KHR_dynamic_rendering`
- QTimer at ~60 FPS drives the render loop on the main thread
- **Re-entrancy guard** (`static bool s_frameInProgress`) prevents nested `DrawFrame()` calls
- **Generation counter** on `Swapchain` ensures command buffers are re-recorded after swapchain recreation

## Dock Layout (ADS)

```
ads::CDockManager
├── CenterDockWidgetArea: Viewport (QVulkanWindow via QWidget::createWindowContainer)
├── LeftDockWidgetArea:   Shader Editor
├── RightDockWidgetArea:  Outliner | Property Editor | Render Config (tabbed)
└── BottomDockWidgetArea: Texture Viewer
```

### Viewport Embedding

The `QVulkanWindow` is embedded into the dock widget via `QWidget::createWindowContainer()`:

```cpp
auto* vulkanWindow = new VulkanWindow(&qVkInstance, vertSpv, vertSize, fragSpv, fragSize);
QWidget* container = QWidget::createWindowContainer(vulkanWindow);
mainWindow->createViewportDock(container);
```

### Layout Persistence

- **View → Save Layout** (`Ctrl+Shift+S`): Serializes dock state to `<appdir>/layout.ads`
- **View → Restore Default Layout**: Deletes non-viewport docks, re-creates default arrangement
- **Auto-load**: `LoadLayout()` called in constructor — restores saved state on startup if available
- Viewport dock is identified by `setObjectName("ViewportDock")` for `restoreState()` matching
- Viewport created first in `CreateDocks()` (ADS requires central widget as first dock)

### Dock Features

- Viewport: closable disabled, movable/floatable enabled (can drag to float or any edge)
- All other docks: closable + movable + floatable
- Config flags: `OpaqueSplitterResize=false` (better Vulkan container behavior), `FocusHighlighting=true`

## Menu Bar

| Menu | Items |
|------|-------|
| **File** | Exit (`Alt+F4`) |
| **View** | Save Layout (`Ctrl+Shift+S`), Restore Default Layout |
| **Help** | About Neurus |

## Build Integration

```cmake
# ADS submodule (static build)
set(ADS_VERSION "4.5.0")
set(BUILD_EXAMPLES OFF)
set(BUILD_STATIC ON)
add_subdirectory(dep/qtadvanceddocking)

# Link targets
target_link_libraries(Neurus PRIVATE ads::qtadvanceddocking-qt6 ...)
target_compile_definitions(Neurus PRIVATE ADS_STATIC)
```

## Layer Isolation

### ✅ UI MAY:
- Own QVulkanInstance, VulkanWindow, QMainWindow
- Emit EventBus signals
- Handle Qt events (resize, close, menu actions)
- Manage ADS dock layout

### ❌ UI MUST NOT:
- Directly call Renderer methods (go through EventBus)
- Create Vulkan objects beyond QVulkanInstance
- Mutate scene state directly
- Access GPU resources directly

## Legacy Code

The following files are retained as reference implementations but are no longer the primary rendering path:
- `VulkanWidget.h/cpp` — vk::raii surface via native HWND
- `MainWindow.h/cpp` — original QWindow subclass
- `main.qml` — original QML window
- `Renderer.h/cpp` — vk::raii render loop with dynamic rendering
- `Swapchain.h/cpp` — manual vk::raii swapchain management
