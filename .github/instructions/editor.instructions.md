# Editor Layer

## Overview

The Editor layer contains **application logic and scene mutation**. It owns the
Controllers, manages user input, maintains selections, and orchestrates state
changes through the event system.

## Location

- `src/editor/Input.h` - InputState struct + GetInputState() / UpdateState()
- `src/editor/events/UIEvents.h` - QObject singleton for UI↔Editor signals
- `src/editor/events/EventQueue.h` - Typed EventQueue for Editor↔Renderer event dispatch
- `src/editor/events/CameraEvents.h` - Camera event structs (rotate, zoom, push, slide)
- `src/editor/Editor.h` - Editor orchestrator (owns Project, Context, Controllers)
- `src/editor/EditorContext.h` - Editor + scene state container
- `src/editor/controllers/Controllers.h` - Base class for all controllers
- `src/editor/controllers/CameraController.h` - Event-driven camera manipulation (orbit/zoom/dolly/pan)

## Core Responsibilities

1. **Event Management**
   - **UIEvents**: QObject singleton with Qt signals for UI↔Editor communication
     (newFrame, windowResized, deviceLost, validationMessage, etc.)
   - **EventQueue**: Typed event dispatcher for Editor↔Renderer event dispatch (no Qt
     dependency). Events are enqueued on `enqueue()` and dispatched on `Process()`.

2. **Context Provisioning**
   - `EditorContext` aggregates scene state and editor state
   - Provides immutable data to Renderer
   - Updated by Editor logic, read by other layers

3. **Controller Orchestration**
   - `Controllers` base class: `virtual Init(EventQueue& bus)` to bind controller to event bus
   - `Editor::RegisterController<T>(EventQueue& bus)` — template factory that creates controller, calls `Init(bus)`, stores in `m_controllers`
   - `CameraController` — event-driven orbit/zoom/dolly/pan via `CameraEvents` (rotate, push, slide, zoom)
   - Controllers receive discrete events (not per-frame polling); `Editor::Edit()` translates raw `InputState` into typed events

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
    void newFrame();
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

### EventQueue (Typed Event Dispatcher)

```cpp
// Subscribe to typed events
EventQueue().subscribe<ObjectSelected>([](const ObjectSelected& e) {
    inspector.showEntity(e.objectId);
});

// Enqueue events (deferred dispatch)
EventQueue().enqueue(ObjectSelected{42});

// Process all queued events (call once per frame)
EventQueue().Process();
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

### Controllers (Base Class)

```cpp
class Controllers
{
public:
    virtual ~Controllers() = default;
    virtual void Init(EventQueue& bus) = 0;
};
```

**Design:**
- Pure virtual base class for all editor controllers
- `Init(EventQueue& bus)` receives the event bus for subscription
- Derived classes subscribe to typed events in `Init()` (e.g. `bus.subscribe<CameraEvents>()`)
- Stored via `std::unique_ptr<Controllers>` in Editor's controller list

### Editor::RegisterController<T>()

```cpp
template<typename T>
void Editor::RegisterController(EventQueue& bus)
{
    auto ctrl = std::make_unique<T>();
    ctrl->Init(bus);
    m_controllers.push_back(std::move(ctrl));
}
```

**Design:**
- Template factory: creates controller, calls `Init(bus)`, stores ownership
- Called during Editor initialization for each controller type
- Controllers are event-driven — no per-frame polling required

### Editor::Edit() — Input Translation

`Editor::Edit(const InputState& input)` translates raw `InputState` (mouse deltas,
modifier keys) into typed event structs and dispatches them through the EventQueue.
This is the bridge between raw Qt input and controller logic:

```
Input::GetInputState() → Editor::Edit(input)
                          ├── CameraEvents enqueued on EventQueue
                          └── EventQueue().Process()
                                └── CameraController handles each event
```

### CameraController (Event-Driven)

```cpp
void CameraController::Init(EventQueue& bus)
{
    bus.subscribe<CameraZoomEvent>([this](const CameraZoomEvent& e) { Zoom(e); });
    bus.subscribe<CameraRotateEvent>([this](const CameraRotateEvent& e) { Rotate(e); });
    bus.subscribe<CameraPushEvent>([this](const CameraPushEvent& e) { Push(e); });
    bus.subscribe<CameraSlideEvent>([this](const CameraSlideEvent& e) { Slide(e); });
}
```

**Design:**
- Event-driven: no per-frame `Update()` polling needed
- Receives discrete camera events (rotate, zoom, push, slide) from Editor
- Each event carries the camera pointer and delta magnitude
- Operates on Camera* provided by each event — does not own the camera
- Located in `src/editor/controllers/CameraController.h`

## Data Flow

```
User Input (QML) → UIEvents Signal → Editor Subscriber → State Update
                                                              ↓
                                                       EventQueue.enqueue
                                                              ↓
                                                       EventQueue.Process()
                                                              ↓
                                                       UI Refresh
```

**3-line newFrame render loop:**
```
UIEvents::newFrame() → Editor::Edit(input) → Renderer::DrawFrame(scene)
```

**Example: Window Resize**
1. QWindow detects resize in MainWindow
2. MainWindow emits `UIEvents::windowResized(w, h)`
3. Renderer slot connected → `Swapchain::Recreate(w, h)`
4. Next `newFrame()` → renders at new resolution

## Architectural Boundaries

### ✅ Editor MAY:
- Mutate scene objects (future)
- Own Controllers and managers
- Emit and subscribe to UIEvents signals and EventQueue typed events
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
- Sends Editor events via EventQueue (typed event dispatcher)
- Renderer NEVER calls back into Editor directly
- One-way dependency: Editor → Renderer (via EventQueue)

**With UI:**
- UI emits signals via UIEvents
- Editor subscribes to UI-relevant signals
- UI reads EditorContext for display (future)
- Two-way via UIEvents, NOT direct C++ calls

**With Data & Resource:**
- Editor may request resource creation (e.g., "load this mesh")
- Data layer handles allocation, returns handle
- Editor stores handle, passes to Renderer

## Current Scope (Deferred PBR MVP)

- UIEvents singleton with UI↔Editor signals (newFrame, windowResized, etc.)
- EventQueue typed event dispatcher for Editor↔Renderer events
- EditorContext stub (empty, placeholder for future scene state)
- Editor orchestrator with `RegisterController<T>()` and `Edit()` for input translation
- Controllers base class with `Init(EventQueue& bus)` virtual interface
- CameraController: event-driven orbit/zoom/dolly/pan via CameraEvents
- Input system: `GetInputState()` returns complete `InputState` for `Edit()` consumption

## Future Enhancements

- SelectionManager (object picking, multi-select)
- Undo/redo system (Command pattern)
- Scene loading/saving orchestration
- Transform gizmo interaction
