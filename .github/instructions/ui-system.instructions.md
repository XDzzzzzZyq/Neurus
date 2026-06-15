# UI System

## Overview

The UI layer is a **pure presentation layer** built on Qt6 QML. It provides the
application window, captures user input, and communicates with other layers
exclusively through the EventBus (Qt Signals/Slots).

## Location

- `src/ui/MainWindow.h` — QWindow subclass with Vulkan surface creation
- `src/ui/resources.qrc` — Qt resource file for QML assets
- `src/ui/qml/main.qml` — Main QML UI shell

## Core Responsibilities

1. **Window Management**
   - Create and manage the application window
   - Create VkSurfaceKHR from the window (passed to Renderer)
   - Handle window resize, minimize, restore, close events
   - Set window title, initial size (800×600), icon

2. **EventBus Exposure**
   - Expose EventBus singleton to QML via context property
   - QML can emit signals (e.g., menu actions, button clicks)
   - QML can connect to signals (e.g., status updates, validation messages)

3. **User Input Handling**
   - Capture mouse, keyboard, and touch events
   - Emit EventBus signals for state changes
   - Future: interactive widgets (sliders, buttons, property editors)

4. **Visual Feedback** (future)
   - Status bar showing GPU info, FPS
   - Scene hierarchy tree (Outliner)
   - Property panels
   - Menu bar

## Architecture Pattern

### MainWindow

```cpp
class MainWindow : public QObject {
    Q_OBJECT
    Q_PROPERTY(int width READ width NOTIFY widthChanged)
    Q_PROPERTY(int height READ height NOTIFY heightChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

public:
    explicit MainWindow(QVulkanInstance* vulkanInstance,
                        EventBus* bus,
                        QObject* parent = nullptr);
    ~MainWindow();

    const vk::raii::SurfaceKHR& surface() const;

signals:
    void widthChanged(int width);
    void heightChanged(int height);
    void titleChanged(QString title);

private:
    std::unique_ptr<QWindow> m_window;
    std::unique_ptr<vk::raii::SurfaceKHR> m_surface;
};
```

**Design:**
- QObject subclass (exposable to QML, supports signals/slots)
- Owns the QWindow (set to Vulkan surface type)
- Creates VkSurfaceKHR from QWindow's native handle (HWND on Windows)
- Resize events → rebuild surface → emit `EventBus::windowResized()`
- RAII: QWindow and VkSurfaceKHR cleaned up in destructor
- Non-copyable

### QVulkanInstance Strategy

Qt6's `QVulkanInstance` creates its own `VkInstance`. We share this instance:
1. MainWindow creates `QVulkanInstance` with API version 1.4
2. `QVulkanInstance::vkInstance()` provides the VkInstance handle
3. VulkanContext creates `vk::raii::Device` from this shared instance
4. MainWindow creates `vk::raii::SurfaceKHR` from QWindow's HWND

This avoids dual-instance complexity while maintaining layer isolation.

### EventBus QML Integration

```qml
// In main.qml:
ApplicationWindow {
    // C++ EventBus exposed as context property
    id: mainWindow

    Timer {
        interval: 16  // ~60 FPS
        running: true
        repeat: true
        onTriggered: EventBus.renderRequested()
    }
}
```

### QML File Structure

```
src/ui/
├── MainWindow.h
├── MainWindow.cpp
├── EventBus.h
├── EventBus.cpp
├── resources.qrc
└── qml/
    └── main.qml      # Main application window
```

## Event Emission Patterns

**Direct Signal from QML:**
```qml
Timer {
    onTriggered: EventBus.renderRequested()
}
```

**Property Binding:**
```qml
Label {
    text: "GPU: " + EventBus.gpuName
}
```

**Signal from C++ to QML:**
```cpp
// In MainWindow::resizeEvent
emit EventBus::instance().windowResized(newWidth, newHeight);
```

## Architectural Boundaries

### ✅ UI MAY:
- Own QWindow and VkSurfaceKHR
- Emit EventBus signals
- Display data via QML bindings
- Handle Qt input events

### ❌ UI MUST NOT:
- Directly call Renderer methods (go through EventBus)
- Create Vulkan objects beyond VkSurfaceKHR
- Mutate scene state directly
- Include headers from `src/render/`
- Access GPU resources directly

## QML Theming (Future)

- Color scheme configuration via Qt styles
- Support light/dark themes
- Custom font application
- Consistent component styling

## Performance Considerations

- QML is declarative — efficient binding evaluation by Qt engine
- Keep QML logic minimal; push complexity to C++
- Timer-driven rendering at target frame rate
- Avoid heavy computations in QML signal handlers

## Gizmo System (Future)

For the 3D viewport:
- Translate/rotate/scale gizmos rendered via Vulkan (not QML)
- Math handled by C++ controllers
- QML provides overlay menus and toolbars

## Extension Points

**Adding a New QML Panel:**
1. Create `.qml` file in `src/ui/qml/`
2. Add to `resources.qrc`
3. Load in `main.qml` or via C++ QML engine
4. Connect to EventBus signals for data flow

**Adding a New EventBus Signal:**
1. Add `signal` to `EventBus` header
2. Emit from C++ or call from QML
3. Connect slots in appropriate layer

## Future Enhancements

- Docking system for editor panels
- Menu bar with file operations (open, save, export)
- Property editor panels (transform, material)
- Outliner (scene hierarchy tree)
- Viewport with Vulkan rendering (QQuickRenderControl)
- Status bar with GPU info, FPS counter
- Settings/preferences panel
