# UI System

## Overview

The UI layer is a **Qt6 Widgets** application with **Qt-Advanced-Docking-System (ADS)** for dockable panels and **QVulkanWindow** for GPU rendering. It communicates with other layers exclusively through the EventBus (Qt Signals/Slots).

## Location

- `src/ui/UIManager.h/cpp` - QMainWindow subclass with ADS dock manager + menus
- `src/ui/panels/UIPanel.h` - Base class for all dock panels with PanelType enum
- `src/ui/panels/Viewport.h/cpp` - Native HWND Vulkan surface widget (Viewport dock)
- `src/ui/panels/Outliner.h/cpp` - Scene object hierarchy tree (Outliner dock)
- `src/ui/panels/PropertyEditor.h/cpp` - Object property inspector (Property Editor dock)
- `src/ui/panels/RenderConfigPanel.h/cpp` - Live render config controls (Render Config dock)
- `src/ui/items/ScalarSlider.h/cpp` - Reusable slider+spinbox composite widget with auto-derived step/decimals
- `src/ui/UIContext.h` - Per-frame UI data snapshot (carries RenderConfig pointer)
- `src/ui/VulkanWindow.h/cpp` - QVulkanWindow subclass hosting the triangle renderer
- `src/ui/MainWindow.h/cpp` - (legacy) QWindow subclass
- `src/ui/VulkanWidget.h/cpp` - (legacy) QWidget subclass with native HWND for vk::raii surface
- `src/ui/qml/main.qml` - (legacy) QML source
- `src/render/QVulkanRenderer.h/cpp` - QVulkanWindowRenderer implementation (triangle pipeline)

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
- **Auto-load**: `LoadLayout()` called in constructor - restores saved state on startup if available
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


## Panel System

All dock panels inherit from `UIPanel` (`src/ui/panels/UIPanel.h`):
- `PanelType` enum identifies each panel (Viewport, Outliner, PropertyEditor, RenderConfig)
- `virtual void Refresh(const UIContext& ctx)` — called per-frame to sync panel UI with application state
- `UIContext` carries a `const void* renderConfig` pointer; panels cast to `const RenderConfig*`
- `UIManager` stores panels in `std::map<PanelType, ads::CDockWidget*> m_panelDocks`

### RenderConfigPanel

`RenderConfigPanel` provides live-adjustable render settings organized in collapsible `QGroupBox` sections:
- **Shadows**: Algorithm (None/ShadowMapping/SDFSoftShadow/VSSM), PCF filter mode, bias `ScalarSlider` (0.0–0.1, derived step 0.0001, 4 decimals)
- **Ambient Occlusion**: Algorithm (None/SSAO), kernel size spinbox, radius `ScalarSlider` (0.0–5.0)
- **Lighting**: IBL toggle, exposure `ScalarSlider` (0.0–5.0)
- **Post-Processing**: Anti-aliasing combo, gamma `ScalarSlider` (1.0–3.0)
- **Pipeline**: Pipeline type (Forward/Deferred), SSR mode, samples per frame

Each control change emits `configValueChanged(RenderConfig cfg)`, wired by `Application` to `Editor::SetRenderConfig(cfg)`.

### ScalarSlider

`ScalarSlider` (`src/ui/items/ScalarSlider.h`) is a reusable composite `QWidget`:
- Pairs a `QSlider` (int) with a `QDoubleSpinBox` in a `QHBoxLayout`
- Bidirectional sync with `blockSignals` to prevent feedback loops
- Emits a single `valueChanged()` signal regardless of which control moved
- Step and decimals auto-derived: `step = (max-min)/sliderSteps`, `decimals = ceil(-log10(step))`
- Tick marks enabled with interval = `max(1, sliderSteps/10)`

### ✅ UI MAY:
- Emit EventBus signals
- Handle Qt events (resize, close, menu actions)
- Manage ADS dock layout

### ❌ UI MUST NOT:
- Own QVulkanInstance, VulkanWindow, QMainWindow
- Directly call Renderer methods (go through EventBus)
- Create Vulkan objects beyond QVulkanInstance
- Mutate scene state directly
- Access GPU resources directly
- Redefine the UI elements that are already in `ui/items`

## Legacy Code

The following files are retained as reference implementations but are no longer the primary rendering path:
- `VulkanWidget.h/cpp` - vk::raii surface via native HWND
- `MainWindow.h/cpp` - original QWindow subclass
- `main.qml` - original QML window
- `Renderer.h/cpp` - vk::raii render loop with dynamic rendering
- `Swapchain.h/cpp` - manual vk::raii swapchain management
