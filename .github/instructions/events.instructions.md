# Event System

## Overview

Neurus uses a **two-tier event system** to enforce layer isolation. UI panels
emit Qt signals; a template bridge in Application converts these to typed
event structs; Editor subscribes and handles them through an internal
EventQueue. The result: panels never include Editor headers, Editor has zero
Qt dependencies, and all cross-layer wiring is auditable in one file.

```
Panel Qt signal (e.g. Outliner::objectSelected)
  └── Application::ConnectUIEvent<Outliner, ObjectSelected>
        └── [lambda calls Editor::OnUIEvent(e)]
              └── EventQueue::enqueue<ObjectSelected>
                    └── EventQueue::Process() → subscriber handles it
```

## Location

| File | Purpose |
|------|---------|
| `src/editor/events/EditorEvents.h` | Editor-scope event structs (ObjectSelected, VisibilityChanged) — pure data, no Qt |
| `src/editor/events/CameraEvents.h` | Camera input events (rotate, push, slide, zoom, resize) |
| `src/editor/events/InputEvents.h` | Viewport raw input events (mouse move, press, release, scroll) |
| `src/editor/events/ProjectEvents.h` | Project lifecycle events (new, open, save, saveAs) |
| `src/editor/events/AssetEvents.h` | Asset import events (mesh, camera, light add) |
| `src/editor/events/ConfigEvents.h` | Render config change events |
| `src/editor/events/ShaderEvents.h` | Shader editor events (create, compile, code/struct edit, field add) |
| `src/editor/events/UIEvents.h` | QObject singleton — Qt signal definitions for UI→Editor dispatch |
| `src/editor/events/EventBus.h` | Header-only typed event dispatcher (`EventQueue` class, zero Qt dependency) |

## Event Structs (Pure Data)

All event structs live in `src/editor/events/` and carry no Qt headers.
They are plain C++ structs passed by const reference through the system.

### EditorEvents.h

```cpp
struct ObjectSelected {
    int       objectId;
    Modifiers modifiers;   // shift/ctrl/alt flags captured at click time
};

struct VisibilityChanged {
    int  objectId;
    bool viewportVisible;
    bool renderVisible;
};
```

### CameraEvents.h

```cpp
struct CameraRotateEvent  { Camera* camera; float dx; float dy; };
struct CameraZoomEvent    { Camera* camera; float delta; };
struct CameraPushEvent    { Camera* camera; float delta; };
struct CameraSlideEvent   { Camera* camera; float dx; float dy; };
struct CameraResizeEvent  { Camera* camera; int w; int h; };
```

### InputEvents.h

```cpp
struct MouseMoveEvent   { int x, y; float dx, dy; Modifiers mods; };
struct MousePressEvent  { int x, y; MouseButton btn; Modifiers mods; };
struct MouseReleaseEvent{ int x, y; MouseButton btn; Modifiers mods; };
struct MouseScrollEvent { int x, y; float delta; Modifiers mods; };
```

### ShaderEvents.h

Events for the shader editor pipeline (UI → Editor → ShaderController). All events
carry a `const ObjectID*` resolved once by the ShaderEditorPanel from the active
scene selection. `ShaderSection` identifies which `ShaderStruct` container is edited.

```cpp
enum class ShaderSection : int {
    Attributes = 0, PassOutputs = 1, Inputs = 2, Outputs = 3,
    Uniforms = 4, StructDefs = 5, Functions = 6, PushConstants = 7
};

struct ShaderCreateRequested  { const ObjectID* object; };
struct ShaderCompileRequested { const ObjectID* object; int stage; int unitType; };  // 0=Code path, 1=Struct path
struct ShaderCodeEdited       { const ObjectID* object; int stage; std::string code; };
struct ShaderStructEdited     { const ObjectID* object; int stage; ShaderSection section;
                                int fieldIndex; int subFieldIndex; std::string field; std::string value; };
struct ShaderFieldAdded       { const ObjectID* object; int stage; ShaderSection section; int subFieldIndex; };
```

**Version flow:** Only `ShaderCreateRequested` and `ShaderCompileRequested` bump
`Shader::m_version` (and only on successful compile). The other events mutate
data without recompiling — the user must press Compile to apply changes to the
GPU pipeline. Because create/compile rebuild the pipeline, `ShaderController`
enqueues `RenderResetEvent` after both, so temporal accumulation resets.

## UIEvents (Qt Signal Bus)

A QObject singleton providing typed Qt signals. Panels connect to these
signals directly; Editor never touches them — the bridge is in Application.

```cpp
class UIEvents : public QObject {
    Q_OBJECT
public:
    static UIEvents& instance();
};

// Profiling is always collected while the app runs: Application calls
// DeferredRenderer::SetProfilingEnabled(true) at startup and forwards the
// FrameProfile returned by DrawFrame() to Editor::SetFrameProfile(), which
// exposes it to panels through UIContext::frameProfile (no UIEvents signal).
//
// Panel signals (defined on the panel itself, not UIEvents):
//   Outliner::objectSelected(const ObjectSelected& e)
//   Outliner::visibilityChanged(const VisibilityChanged& e)
//   RenderConfigPanel::configValueChanged(const RenderConfigChangedEvent& e)
//   Viewport::mouseMoved(const MouseMoveEvent& e)
//   ...
```

## EventQueue (Typed Dispatcher, No Qt)

Header-only template-based dispatcher for Editor-internal event routing.
Events are enqueued and batch-processed via `Process()`.

```cpp
// Subscribe to typed events
eventBus.subscribe<ObjectSelected>([](const ObjectSelected& e) {
    inspector.showEntity(e.objectId);
});

// Enqueue events (deferred dispatch)
eventBus.enqueue(ObjectSelected{42, modifiers});

// Process all queued events (call once per frame or on input)
eventBus.Process();
```

**Properties:**
- Header-only, zero Qt dependency — safe to include from any layer
- Deferred FIFO queue: `enqueue()` stores, `Process()` dispatches
- Re-entrant safe: events emitted from handlers are appended to the queue
- Max events cap (default 1000) prevents infinite loops
- Not thread-safe — all calls on main thread

## Template-Based Event Forwarding

The bridge from Qt signals to the Editor's internal EventQueue uses two
template helpers in `Application`:

### Step 1 — Panel defines a Qt signal

```cpp
// Outliner.h
signals:
    void objectSelected(const ObjectSelected& e);
    void visibilityChanged(const VisibilityChanged& e);
```

### Step 2 — Application wires via ConnectUIEvent

```cpp
// Application.h
template<typename Panel, typename Event>
void ConnectUIEvent(QObject* sender, void (Panel::*signal)(const Event&))
{
    QObject::connect(
        static_cast<Panel*>(sender), signal,
        [editor = app_editor.get()](const Event& e) {
            editor->OnUIEvent(e);
        });
}

// Application.cpp — one line per panel signal
ConnectUIEvent(outliner, &Outliner::objectSelected);
ConnectUIEvent(outliner, &Outliner::visibilityChanged);
ConnectUIEvent(viewport, &Viewport::mouseMoved);
ConnectUIEvent(viewport, &Viewport::mouseScrolled);
```

### Step 3 — Editor::OnUIEvent enqueues

```cpp
// Editor.h
template<typename Event>
void OnUIEvent(const Event& e) {
    ed_eventBus.enqueue<Event>(e);
}
```

### Step 4 — Editor subscribes

```cpp
// Editor.cpp constructor
ed_eventBus.subscribe<ObjectSelected>([this](const ObjectSelected& e) {
    SelectObject(e.objectId, e.modifiers.shiftOrCtrl());
});
ed_eventBus.subscribe<VisibilityChanged>([this](const VisibilityChanged& e) {
    ChangeObjectVisibility(e.objectId, e.viewportVisible, e.renderVisible);
});
```

## Data Flow Summary

```
User Input (QML / Qt widgets)
    │
    ▼
UI Panel (Qt signal, e.g. Outliner::objectSelected)
    │
    ▼
Application::ConnectUIEvent<T>   ← single wiring point
    │  [lambda calls editor->OnUIEvent(e)]
    ▼
Editor::OnUIEvent<T>             ← generic template
    │  eventBus.enqueue<T>(e)
    ▼
EventQueue<T>::Process()         ← batch dispatch
    │
    ▼
Editor subscriber                ← business logic
    ├── SelectObject()
    ├── ChangeObjectVisibility()
    └── SetRenderConfig()
```

## Adding a New Event

1. **Define the event struct** in the appropriate `src/editor/events/*.h` file
   (pure data, no Qt includes).
2. **Emit it from the panel** as a Qt signal.
3. **Wire it in Application** with one `ConnectUIEvent` call.
4. **Subscribe in Editor** with one `eventBus.subscribe<T>()` call.

That's it — four changes, all in different layers, no cross-layer includes needed.

### RenderResetEvent

```cpp
struct RenderResetEvent
{
};
```

An empty marker event emitted whenever the scene state changes in a way that
invalidates temporal accumulation (camera movement, light/object transform,
visibility toggle, config change, project load, asset import, environment change,
shader create/compile). Camera movement events are enqueued by `CameraController`
after each frame handler (Zoom, Rotate, Push, Slide, Resize). Shader create and
compile events are enqueued by `ShaderController` after the shader version bumps.
Other scene mutations enqueue it directly from `Editor` event handlers.

**Subscribers** listen for this event to reset per-frame history:
- `DeferredRenderer::ResetShadowAccumulation()` — zeros the shadow accumulation iteration counter.
- Future: SSAO temporal accumulation, SSR temporal accumulation, TAA jitter reset.
