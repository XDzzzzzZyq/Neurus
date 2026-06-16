# Editor Layer

## Overview

The Editor layer contains **application logic and scene mutation**. It owns the
Controllers, manages user input, maintains selections, and orchestrates state
changes through the event system.

## Location

- `src/editor/events/UIEvents.h` - QObject singleton for UI↔Editor signals
- `src/editor/events/EventBus.h` - Typed EventPool for Editor↔Renderer events
- `src/editor/events/EditorEvents.h` - Event type structs (ObjectSelected, etc.)
- `src/editor/EditorContext.h` - Editor + scene state container
- `src/editor/controllers/` - Specific controller implementations (future)

## Core Responsibilities

1. **Event Management**
   - **UIEvents**: QObject singleton with Qt signals for UI↔Editor communication
     (renderRequested, windowResized, deviceLost, validationMessage, etc.)
   - **EventBus**: Typed EventPool for Editor↔Renderer event dispatch (no Qt
     dependency). Events are enqueued on `enqueue()` and dispatched on `Process()`.

2. **Context Provisioning**
   - `EditorContext` aggregates scene state and editor state
   - Provides immutable data to Renderer
   - Updated by Editor logic, read by other layers

3. **Controller Orchestration** (future)
   - CameraController - Camera movement and view manipulation
   - SelectionController - Object selection management
   - ViewportController - Viewport interaction and rendering settings

4. **Scene State Management** (future)
   - Create, modify, delete scene objects
   - Update transforms, materials, parameters
   - Manage scene hierarchy

## Key Components

### UIEvents (Qt Signal Bus)

```cpp
class UIEvents : public QObject {
    Q_OBJECT
public:
    static UIEvents& instance();

signals:
    void renderRequested();
    void windowResized(int width, int height);
    void deviceLost();
    void validationMessage(QString severity, QString message);

private:
    UIEvents() = default;
};
```

**Design:**
- Singleton pattern via `instance()` static method
- Qt's signal/slot mechanism provides type-safe dispatch
- Signals are implicitly thread-safe (Qt handles queued connections)
- Multiple slots can connect to the same signal
- QML exposure via `rootContext()->setContextProperty("UIEvents", &ui)`

### EventBus (Typed EventPool)

```cpp
// Subscribe to typed events
EventBus().subscribe<ObjectSelected>([](const ObjectSelected& e) {
    inspector.showEntity(e.objectId);
});

// Enqueue events (deferred dispatch)
EventBus().enqueue(ObjectSelected{42});

// Process all queued events (call once per frame)
EventBus().Process();
```

**Design:**
- Header-only template-based dispatcher (zero Qt dependency)
- Deferred FIFO queue: `enqueue()` stores, `Process()` dispatches
- Re-entrant safe: events emitted from handlers are appended to queue
- Max events cap (default 1000) prevents infinite loops
- Not thread-safe - all calls on main thread

### EditorContext

```cpp
class EditorContext : public QObject {
    Q_OBJECT
public:
    explicit EditorContext(QObject* parent = nullptr);

    // Scene state (immutable view for Renderer)

    // Editor state

    // Selection state (future)

private:
    // Scene graph (future)
    // Selection manager (future)
};
```

**Design:**
- Pure data class - holds state, no rendering logic
- QObject subclass for signal/slot and QML exposure
- Non-copyable
- RAII - constructor fully initializes, destructor cleans up

## Data Flow

```
User Input (QML) → UIEvents Signal → Editor Subscriber → State Update
                                                              ↓
                                                       EventBus.enqueue
                                                              ↓
                                                       EventBus.Process()
                                                              ↓
                                                       UI Refresh
```

**Example: Window Resize**
1. QWindow detects resize in MainWindow
2. MainWindow emits `UIEvents::windowResized(w, h)`
3. Renderer slot connected → `Swapchain::Recreate(w, h)`
4. Next `renderRequested()` → renders at new resolution

## Architectural Boundaries

### ✅ Editor MAY:
- Mutate scene objects (future)
- Own Controllers and managers
- Emit and subscribe to UIEvents signals and EventBus typed events
- Update EditorContext state
- Call into scene management systems

### ❌ Editor MUST NOT:
- Directly manipulate GPU resources
- Call Vulkan functions
- Depend on UI implementation details (only UIEvents signals)
- Store rendering-specific state (belongs in Renderer or Data/Resource layer)

## Integration with Other Layers

**With Renderer:**
- Provides scene data via EditorContext
- Receives rendering events via UIEvents (Qt signals)
- Sends Editor events via EventBus (typed event pool)
- Renderer NEVER calls back into Editor directly
- One-way dependency: Editor → Renderer (via EventBus)

**With UI:**
- UI emits signals via UIEvents
- Editor subscribes to UI-relevant signals
- UI reads EditorContext for display (future)
- Two-way via UIEvents, NOT direct C++ calls

**With Data & Resource:**
- Editor may request resource creation (e.g., "load this mesh")
- Data layer handles allocation, returns handle
- Editor stores handle, passes to Renderer

## Current Scope (Triangle MVP)

- UIEvents singleton with UI↔Editor signals (renderRequested, windowResized, etc.)
- EventBus typed EventPool for Editor↔Renderer events
- EditorContext stub (empty, placeholder for future scene state)
- No controllers yet
- No selection manager yet

## Future Enhancements

- CameraController (orbit, pan, zoom)
- SelectionManager (object picking, multi-select)
- Undo/redo system (Command pattern)
- Scene loading/saving orchestration
- Transform gizmo interaction
