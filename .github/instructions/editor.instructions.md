# Editor Layer

## Overview

The Editor layer contains **application logic and scene mutation**. It owns the
Controllers, manages user input, maintains selections, and orchestrates state
changes through the event system.

## Location

- `src/editor/Input.h` - InputState struct + GetInputState() / UpdateState()
- `src/editor/Editor.h` - Editor orchestrator (owns Scene, RenderConfig, Context, Controllers)
  - Exposes an explicit scene-load lifecycle for Application-driven persistence:
    `NewScene()` (empty project), `BeginLoad()` (WaitIdle + fresh Scene/RenderConfig)
    and `FinishLoad()` (reload mesh data, upload, IBL). Editor no longer owns the
    project file path or performs Save/LoadProject — `Application` coordinates
    persistence and owns the project path + dirty aggregation (`Editor::IsDirty()` /
    `ClearDirty()` still track scene/config edits).
- `src/editor/EditorContext.h` - Editor + scene state container
- `src/editor/controllers/Controllers.h` - Base class for all controllers
- `src/editor/controllers/CameraController.h` - Event-driven camera manipulation (orbit/zoom/dolly/pan)
- `src/editor/controllers/SceneController.h/cpp` - Event-driven scene mutations (selection, transform, visibility, property edits); emits EditorEvents for GPU uploads
- `src/editor/controllers/ShaderController.h` - Event-driven shader lifecycle (create, compile, code/struct edit, field add)
- `src/editor/events/ShaderEvents.h` - Shader editor event structs (see events.instructions.md)

## Core Responsibilities

1. **Event Management** — See `.github/instructions/events.instructions.md` for the
   complete event system: UIEvents singleton, EventQueue typed dispatcher, event
   structs, and the `ConnectUIEvent`/`OnUIEvent` template forwarding pattern.

2. **Context Provisioning**
   - `EditorContext` aggregates scene state and editor state
   - Provides immutable data to Renderer
   - Updated by Editor logic, read by other layers

3. **Controller Orchestration**
   - `Controllers` base class: `virtual Init(EventQueue& bus)` to bind controller to event bus
   - `Editor::RegisterController<T>(EventQueue& bus)` — template factory that creates controller, calls `Init(bus)`, stores in `m_controllers`
   - `CameraController` — event-driven orbit/zoom/dolly/pan via `CameraEvents` (rotate, push, slide, zoom)
   - `SceneController` — event-driven scene mutations (selection, transform, visibility, camera/mesh/light/env property edits); stateless with free-function handlers in the .cpp; emits `EditorEvents` (SceneModified, LightGpuChanged, LightingRebuild, RenderResetEvent) for GPU uploads and dirty tracking; see events.instructions.md for the GPU-sync flow
   - `ShaderController` — event-driven shader lifecycle via `ShaderEvents` (create, compile, code/struct edit, field add); enqueues `RenderResetEvent` after create/compile so temporal accumulation resets
   - Controllers receive discrete events (not per-frame polling); `Editor::Edit()` dispatches all enqueued events via `EventQueue::Process()`

4. **GPU Upload & Asset Import**
   - Editor handles asset import events (mesh, camera, light adds) and performs GPU uploads directly (mesh upload, light SSBO dict update, IBL generation)
   - Editor subscribes to cross-component `EditorEvents` (LightGpuChanged, LightingRebuild, SceneModified) emitted by `SceneController` and executes GPU uploads against its `DeferredRenderer`/`UploadManager`
   - Must NOT directly mutate GPU resources outside of event handlers — scene mutations go through `SceneController`

## Key Components

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
- Called during Editor initialization for each controller type (CameraController, ShaderController, SceneController)
- Controllers are event-driven — no per-frame polling required

### Editor::Edit() — Event Dispatch

`Editor::Edit()` dispatches all enqueued events through the EventQueue. Input
translation (mouse events → camera events) is handled in `Editor::Initialize()`
via `MouseMoveEvent` / `MouseScrollEvent` subscriptions, so `Edit()` is a pure
`EventQueue::Process()` call:

```
Editor::Edit()
  └── EventQueue::Process()
        ├── CameraController handles each event
        ├── SceneController handles each event
        └── etc.
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

### SceneController (Event-Driven)

```cpp
void SceneController::Init(EventQueue& bus)
{
    // Selection, visibility, transform, camera/mesh/light/env property events
    // (17 subscriptions total; see SceneEvents.h)
}
```

**Design:**
- Stateless: all handlers are free functions in an anonymous namespace
- Events carry `const ObjectID*`; handlers cast to the concrete type via the
  class static `As()` helpers (e.g. `Mesh::As(...)`) and mutate directly - no
  Scene lookup by ID
- Selection events (`ObjectSelected`, `ObjectDeselected`) use `const UID*`
  cast to `Scene*` via `Scene::As(...)`
- GPU uploads delegated to Editor via EditorEvents (`LightGpuChanged`,
  `LightingRebuild`, `SceneModified`, `RenderResetEvent`) which Editor
  subscribes to and executes against its `DeferredRenderer`/`UploadManager`
- Located in `src/editor/controllers/SceneController.h` / `.cpp`

### ShaderController (Event-Driven)

```cpp
void ShaderController::Init(EventQueue& bus)
{
    bus.subscribe<ShaderCreateRequested>( [&bus](const ShaderCreateRequested& e) {
        OnCreateShader(e);
        bus.enqueue(RenderResetEvent{});  // pipeline created -> reset accumulation
    });
    bus.subscribe<ShaderCompileRequested>( [&bus](const ShaderCompileRequested& e) {
        OnCompileShader(e);
        bus.enqueue(RenderResetEvent{});  // pipeline rebuilt -> reset accumulation
    });
    bus.subscribe<ShaderCodeEdited>( [](const ShaderCodeEdited& e) { OnCodeEdited(e); });
    bus.subscribe<ShaderStructEdited>( [](const ShaderStructEdited& e) { OnStructEdited(e); });
    bus.subscribe<ShaderFieldAdded>( [](const ShaderFieldAdded& e) { OnFieldAdded(e); });
}
```

**Design:**
- Stateless: all handlers are free functions in an anonymous namespace
- Handlers cast the event's `const ObjectID*` to `Mesh*` (the only shader-owning
  object) and mutate `mesh->o_shader` data directly — no Editor, Renderer, or GPU state
- Create/Compile bump `Shader::m_version` on success (pipeline rebuild); the other
  events only mutate IR/code and require the user to press Compile
- Only Create/Compile enqueue `RenderResetEvent` (temporal accumulation reset)
- Located in `src/editor/controllers/ShaderController.h`

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

- Scene loading/saving orchestration
- Transform gizmo interaction

## Undo/Redo controllers (`src/editor/operations/`)

The operation model, coalescing strategies, and history persistence are
documented in
[operation-system.instructions.md](operation-system.instructions.md). This
section covers only how the editor's controllers *produce* operations.

Two controllers use the explicit begin/end gesture pattern (capture "before" on
a begin event, mutate live without recording, record ONE op on the end event):

- **`CameraController`** — `CameraDragBegin` captures the pose, `CameraRotate/
  Push/Slide` mutate live, `CameraDragEnd` records one `CameraTransformOp`
  (no-op if the pose is unchanged). `CameraTransformOp` is deliberately
  non-mergeable (empty `MergeKey`) so each drag is its own undo entry. Scroll
  zoom has no press/release boundary, so it stays on the implicit-merge path
  via the separate `CameraZoomOp` type (mergeable, keyed `camera_zoom:<uid>`).
  Two op types instead of one boolean flag: the gesture boundary vs. burst
  coalescing are distinct behaviors that belong to distinct ops.
- **`RenderConfigController`** — the single mutation path for the Editor-owned
  `RenderConfig`, reached through a `std::function<RenderConfig*()>` provider so
  it never includes Editor internals (constructed manually in `Editor`, not via
  `RegisterController<T>`). `ConfigEditBegin` captures the config,
  `RenderConfigChangedEvent` applies live, `ConfigEditEnd` records one
  `SetRenderConfigOp`. Discrete edits (checkbox/combo) arrive without a gesture
  and record immediately. No-op writes (`before == after`, via `RenderConfig`'s
  defaulted `operator==`) are never recorded. `SetRenderConfigOp` is scene-level
  (not UID-based) and deliberately non-mergeable (empty `MergeKey`).

