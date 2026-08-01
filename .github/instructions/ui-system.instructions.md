# UI System

## Overview

The UI layer is a **Qt6 Widgets** application with **Qt-Advanced-Docking-System (ADS)** for dockable panels and **QVulkanWindow** for GPU rendering. It communicates with other layers exclusively through the EventBus (Qt Signals/Slots).

## Location

| File | Purpose |
|------|---------|
| `src/ui/UIManager.h/cpp` | QMainWindow subclass with ADS dock manager + menus |
| `src/ui/UIManager.h/cpp` | Per-frame `Refresh()` pipeline, panel registry (`GetPanel<T>`) |
| `src/ui/UIContext.h/cpp` | Per-frame UI data snapshot (carries RenderConfig pointer) |
| `src/ui/panels/UIPanel.h` | Base class for all dock panels with `PanelType` enum |
| `src/ui/panels/Viewport.h/cpp` | Native HWND Vulkan surface widget (Viewport dock) |
| `src/ui/panels/Outliner.h/cpp` | Scene object hierarchy tree (Outliner dock) |
| `src/ui/panels/PropertyPanel.h/cpp` | Object property inspector with GOType-aware subpanels (Property dock) |
| `src/ui/panels/RenderConfigPanel.h/cpp` | Live render config controls (Render Config dock) |
| `src/ui/panels/ShaderEditorPanel.h/cpp` | Shader editor dock — Code mode + Structure mode (tree-based Struct Editor) |
| `src/ui/items/ShaderStructModel.h/cpp` | QAbstractItemModel tree for the ShaderStruct IR (3-level: sections → fields/structs → members) |
| `src/ui/items/ShaderFieldDelegate.h/cpp` | Delegate with QComboBox (type) / QLineEdit (name) editors for struct field rows |
| `src/ui/elements/CodeEditor.h/cpp` | GLSL code editor widget (line numbers, monospace font) |
| `src/ui/elements/ShaderHighlighter.h/cpp` | GLSL syntax highlighter for the code editor |
| `src/ui/presets/CameraProperties.h/cpp` | Camera property editor preset (target Vec3Spin + FOV ScalarSlider) |
| `src/ui/presets/MeshProperties.h/cpp` | Mesh property editor preset (path label + shadow/material checkboxes) |
| `src/ui/presets/LightProperties.h/cpp` | Light property editor preset (type label + power/radius sliders + shadow checkbox) |
| `src/ui/presets/EnvironmentProperties.h/cpp` | Environment property editor preset (path label + intensity/rotation sliders) |
| `src/ui/Icons.h/cpp` | Static SVG icon library with lazy-loaded QIcon cache |
| `src/ui/items/ScalarSlider.h/cpp` | Reusable slider+spinbox composite widget |
| `src/ui/items/Vec3Spin.h/cpp` | Reusable XYZ triple-spinbox composite widget |
| `src/ui/items/OutlinerRow.h/cpp` | Pool-recyclable outliner row with type icon, name, toggles |
| `src/ui/qml/` | Qt resource files: QML layouts, QSS stylesheets (embedded at build time) |
| `src/ui/VulkanWindow.h/cpp` | QVulkanWindow subclass hosting the triangle renderer |

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
  → VulkanContext → vk::raii::Device → Renderer → Swapchain → Shader
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

### ShaderEditorPanel

`ShaderEditorPanel` (Left dock) edits the GLSL shader of the active mesh in two
modes selected by a `QComboBox`:
- **Code mode**: raw GLSL in a `CodeEditor` widget (line numbers, GLSL syntax
  highlighting). Text changes emit `codeEdited`.
- **Structure mode**: a `QTreeView` (`ShaderStructModel` + `ShaderFieldDelegate`)
  showing the parsed `ShaderStruct` IR as a 3-level tree
  (sections → fields/structs → members). Type/name cells are edited inline via
  `QComboBox` / `QLineEdit` editors and emit `structEdited`.

Each section header row and each struct-definition row carries a real
`QPushButton` "+" (`QTreeView::setIndexWidget`), which appends a new entry to
that section (or a member field to that struct) and emits `fieldAdded`. A
toolbar "−" removes the selected entry. Both "Create Shader" (no shader yet)
and "Compile" buttons are available; Compile re-parses/generates/compiles and
bumps the shader version so the GeometryPass pipeline rebuilds.

**State preservation across `Refresh()`:** the tree model is rebuilt on every
shader version change (`setShaderStruct` → model reset), which would wipe
expansion, selection, and index widgets. `populateSections()` captures the
expanded rows and current selection before the rebuild and restores them after,
then re-installs the per-row "+" buttons and column spans. Opened sections stay
open, closed sections stay closed, and the selected row survives edits/adds.

Signals (`createShaderRequested`, `compileRequested`, `codeEdited`,
`structEdited`, `fieldAdded`) are wired by `Application` to `Editor` and
dispatched to `ShaderController` (see events.instructions.md).

## Reusable Items

Items in `src/ui/items/` are self-contained composite widgets used across panels.
They do NOT belong to any specific panel and should be reused rather than
redefined. For type-specific property editors built from items, see
[Property Presets](#property-presets) in `src/ui/presets/`.

### Icons

`Icons` (`src/ui/Icons.h`) is a fully static class providing lazy-loaded QIcon objects:

- **`Icons::Initialize()`** — called once by `UIManager` during construction. Populates the hardcoded path registry (icon name → Qt resource path). Safe to call multiple times (idempotent).
- **`Icons::GetIcon(name)`** — returns `const QIcon&` from an internal cache. On first access, loads the SVG from the Qt resource system; subsequent calls return the cached instance.
- **Naming convention**: `"folder:name"` (e.g. `"scene:mesh"` → `:/icons/scene/mesh.svg`). 10 icons registered across `scene:` and `editor:` namespaces.
- **Path registry**: hardcoded in `Initialize()`, mapping icon names to `:/icons/...` Qt resource paths.
- **Cache**: `std::unordered_map<std::string, QIcon>`, populated lazily per icon name.
- **No instantiation needed** — all members are static. Any code in `src/ui/` can call `Icons::GetIcon(name)` directly.

Icons are embedded in the binary via `qt_add_resources(neurus_ui "icons" PREFIX "/icons" ...)` in `src/ui/CMakeLists.txt`. SVGs live under `res/ui/icons/`.

### ScalarSlider

`ScalarSlider` (`src/ui/items/ScalarSlider.h`) is a reusable composite `QWidget`:
- Pairs a `QSlider` (int) with a `QDoubleSpinBox` in a `QHBoxLayout`
- Bidirectional sync with `blockSignals` to prevent feedback loops
- Emits a single `valueChanged()` signal regardless of which control moved
- Step and decimals auto-derived: `step = (max-min)/sliderSteps`, `decimals = ceil(-log10(step))`
- Tick marks enabled with interval = `max(1, sliderSteps/10)`

### Vec3Spin

`Vec3Spin` (`src/ui/items/Vec3Spin.h`) is a reusable composite `QWidget`:
- Three `QDoubleSpinBox` widgets in a horizontal row (X, Y, Z)
- Configurable range, step, decimals, and suffix
- Emits `valueChanged(x, y, z)` signal when any spinbox changes
- `setValue(x, y, z)` with internal dirty-check — no-ops if all three values unchanged
- Uses `QSignalBlocker` internally to prevent feedback loops during programmatic updates


## Patterns

### Loading QSS Stylesheets from Qt Resources

Stylesheets should be stored as `.qss` files in `src/ui/qml/` and embedded via
`qt_add_resources`, same as QML. Use `setObjectName()` + QSS ID selectors
instead of per-widget `setStyleSheet()` calls:

```
// 1. Embed in CMakeLists.txt
qt_add_resources(neurus_ui "resources" PREFIX "/" FILES qml/outliner.qss)

// 2. Load once (static, one-shot)
QFile file(":/qml/outliner.qss");
file.open(QIODevice::ReadOnly | QIODevice::Text);
QString stylesheet = QTextStream(&file).readAll();

// 3. Apply to parent widget — ID selectors cascade to children
parentWidget->setStyleSheet(stylesheet);

// 4. In C++, set object names matching QSS ID selectors
childBtn->setObjectName("myButton");  // matches QPushButton#myButton

// 5. Per-instance overrides via setStyleSheet() on the specific child
childBtn->setStyleSheet("QPushButton { color: #ff6f00; }");
```

See `src/ui/qml/outliner.qss` for the canonical example using
`QPushButton#outlinerNameBtn` and `QPushButton#outlinerToggleBtn` selectors.

## Performance Optimization

### Lazy Updates via Logical State Tracking

Avoid redundant widget operations by storing the last-known state and skipping
updates when nothing changed:

```cpp
// DO: dirty-check before applying
if (m_eyeVisible != viewportVisible)
{
    m_eyeBtn->blockSignals(true);
    m_eyeBtn->setChecked(viewportVisible);
    m_eyeBtn->blockSignals(false);
    m_eyeVisible = viewportVisible;
    setEyeBtnColor();
}

// DON'T: always apply, even when identical
m_eyeBtn->setChecked(viewportVisible);
setEyeBtnColor();
```

This applies to any `Refresh()`-based panel — cache the previous value of each
widget-modifying input and gate the write behind an equality check.

### Prefer QSS Over Inline Stylesheets

Setting `setStyleSheet()` on individual widgets forces Qt to re-parse the CSS
text and re-resolve all selectors, which is measurably slower than QSS class
selectors with dynamic properties:

```cpp
// DO: QSS with dynamic property (fast, declarative)
//  .qss: QPushButton#outlinerNameBtn[selectionState="active"] { color: #ff6f00; }
m_nameBtn->setProperty("selectionState", "active");
m_nameBtn->style()->unpolish(m_nameBtn);
m_nameBtn->style()->polish(m_nameBtn);

// DON'T: inline stylesheet (slow, re-parses CSS text each time)
m_nameBtn->setStyleSheet("QPushButton { color: #ff6f00; }");
```

The same principle applies to `setIcon()` on toggle buttons — use
`GetIconPair()` to pre-bake On/Off states into a single `QIcon` instead of
string-building icon names and calling `GetIcon()` on every toggle.

### Naming Convention: `set*` for Refresh-Path Methods

Widget methods called from `Refresh()` or other per-frame update paths must
use Qt-style lowercase naming (e.g. `setObject()`, `setValue()`, `setRowIndex()`)
rather than PascalCase. This aligns with Qt's own convention for setter methods
(`QSpinBox::setValue()`, `QWidget::setVisible()`) and distinguishes them from
application-level PascalCase methods (`BuildTransformEditor()`, `PopulateTransform()`).

New reusable items in `src/ui/items/` should follow this convention for all
public setters that participate in the Refresh pipeline.

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
- Redefine the UI elements that are already in `ui/items`

## Legacy Code

The following files are retained as reference implementations but are no longer the primary rendering path:
- `VulkanWidget.h/cpp` - vk::raii surface via native HWND
- `MainWindow.h/cpp` - original QWindow subclass
- `main.qml` - original QML window
- `Renderer.h/cpp` - vk::raii render loop with dynamic rendering
- `Swapchain.h/cpp` - manual vk::raii swapchain management
