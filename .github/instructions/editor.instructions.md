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
    and `FinishLoad()` (upload scene resources, IBL). Editor no longer owns the
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
   - `SceneController` — event-driven scene mutations (selection, transform, visibility, camera/mesh/light/env property edits, scene membership add/delete); stateless with free-function handlers in the .cpp; constructed manually with a `ResourceManager*` provider (UID → object resolution, the `RenderConfigController` pattern); emits `EditorEvents` (SceneModified, LightGpuChanged, LightingRebuild, RenderResetEvent) for GPU uploads and dirty tracking; see events.instructions.md for the GPU-sync flow
   - **Import/Add split**: `Editor::OnMeshImport`/`OnCameraAdd`/`OnLightAdd`/... only LOAD the resource into the pool and forward the object UID via `SceneObjectAddRequested`; the SceneController fetches the pooled object by UID, registers it, selects it, and records `CompositeOp[SceneObjectAddOp({u},true), SetSelectionOp(...)]`. The Delete gesture (`ObjectDeleteRequested` - the Editor wraps the UI's `DeleteRequested` intent and stamps `m_scene.get()`) is **forward-only**: the SceneController snapshots the selection, guards the last camera, deselects, then DEFERS the removals as ONE batched `SceneObjectDeleteRequested` carrying all selected UIDs — so the batched handler is the single removal path shared with replay (no replay-only handling) — and records `CompositeOp[SetSelectionOp(before→∅), SceneObjectAddOp(uids,false)]` (delete of N = one op). Light membership changes enqueue `LightingRebuild` (the SSBO is a scene projection). GPU caches (MeshGPU, shadow maps) are scene-scoped: `UploadSceneResources` uploads only objects present at load, and an object entering the scene (live add or undo/redo replay) or a light whose shadow was just enabled enqueues `SceneObjectGpuUploadRequested`, which the Editor resolves with an on-demand upload (skip-if-cached).
   - `ShaderController` — event-driven shader lifecycle via `ShaderEvents` (create, compile, code/struct edit, field add); enqueues `RenderResetEvent` after create/compile so temporal accumulation resets
   - **Pure-intent wrapping**: the Editor subscribes to the UI's `ObjectClicked` and `DeleteRequested` intents and forwards the dedicated scene events `ObjectSelected{ m_scene.get(), object, modifiers }` and `ObjectDeleteRequested{ m_scene.get() }` (the two wrap subscriptions in `Editor::Initialize`). Panels never stamp the scene.
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
- Called during Editor initialization for CameraController and ShaderController.
  Controllers needing Editor-owned state (a provider) are constructed manually
  instead — `SceneController` takes a `ResourceManager*` provider and
  `RenderConfigController` a `RenderConfig*` provider (see Editor::Initialize).
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
- Events carry `const UID*`; handlers cast to the concrete type via
  `ObjectID::As<T>` (comparing `o_type` against `T::Type`) or the untyped
  `ObjectID::As`, and mutate directly - no Scene lookup by ID
- Selection events (`ObjectSelected`, `ObjectDeselected`) use `const UID*`
  cast to `Scene*` via `Scene::As(...)`
- GPU uploads delegated to Editor via EditorEvents (`LightGpuChanged`,
  `LightingRebuild`, `SceneModified`, `RenderResetEvent`) which Editor
  subscribes to and executes against its `DeferredRenderer`/`UploadManager`
- Located in `src/editor/controllers/SceneController.h` / `.cpp`

### ShaderController (Event-Driven)

```cpp
void ShaderController::Init(EventQueue& bus, IOperationSink& ops)
{
    // Lifecycle (non-undoable): bump Shader::m_version + reset accumulation.
    // (Shader CREATE is handled by the Editor, which constructs the pooled
    // RenderShader and records a ShaderLinkOp - see the Undo/redo controllers
    // section. ShaderController keeps only pool-free handlers.)
    bus.subscribe<ShaderCompileRequested>( [&bus](const ShaderCompileRequested& e) {
        OnCompileShader(e);
        bus.enqueue(RenderResetEvent{});  // pipeline rebuilt -> reset accumulation
    });

    // Code edits apply live; recording is bracketed by ShaderEditBegin/End so a
    // keystroke burst collapses to ONE undo entry on focus-out.
    bus.subscribe<ShaderCodeEdited>( [](const ShaderCodeEdited& e) { OnCodeEdited(e); });
    bus.subscribe<ShaderEditBegin>( /* capture m_beforeCode */ );
    bus.subscribe<ShaderEditEnd>( /* record SetShaderCodeOp if code changed */ );

    // Discrete struct/field edits: snapshot the element, apply the edit, record one delta op each.
    bus.subscribe<ShaderStructEdited>( /* VisitElement + ApplyFieldEdit; Submit SetShaderFieldOp if before != after */ );
    bus.subscribe<ShaderFieldAdded>( /* AppendDefault + Submit AddShaderFieldOp */ );

    // Undo/redo replay: re-apply one edit dimension + bump version (panel refresh).
    bus.subscribe<ShaderCodeRestored>(     [](const ShaderCodeRestored& e)     { OnCodeRestored(e); });
    bus.subscribe<ShaderFieldRestored>(    [](const ShaderFieldRestored& e)    { OnFieldRestored(e); });
    bus.subscribe<ShaderFieldAddRestored>( [](const ShaderFieldAddRestored& e) { OnFieldAddRestored(e); });
    bus.subscribe<ShaderFieldRemoved>(     [](const ShaderFieldRemoved& e)     { OnFieldRemoved(e); });
}
```

**Design:**
- Mutation handlers are free functions in an anonymous namespace; gesture state
  (`m_codeEditing`, `m_editObject`, `m_editStage`, `m_beforeCode`,
  `m_beforeParsed`) lives on the controller so begin/end can bracket a burst
- Handlers cast the event's `const UID*` to `Mesh*` via `ObjectID::As<Mesh>`
  (the only shader-owning object) and mutate `mesh->o_shader` data directly — no Editor, Renderer, or GPU state
- Create/Compile bump `Shader::m_version` on success (pipeline rebuild). Create
  is **undoable** (the Editor records `ShaderLinkOp` - undo drops the pooled
  reference, redo relinks it); Compile stays a **non-undoable** lifecycle
  action. Content edits (code/struct/field) only mutate CPU IR/code and require
  the user to press Compile to reach the GPU
- Only Create/Compile enqueue `RenderResetEvent` (temporal accumulation reset)
- Undo/redo of content edits is CPU-only: `OnRestoreSource` overwrites the stage's
  `code` + `parsed` IR and bumps `ShaderUnit::m_version` (panel refresh) — it does
  **not** recompile to SPIR-V (see Undo/Redo controllers below)
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

Three controllers use the explicit begin/end gesture pattern (capture "before"
on a begin event, mutate live without recording, record ONE op on the end
event):

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
- **`ShaderController`** — content edits to a mesh's shader become undoable via
  three fine-grained delta ops, each carrying only its before/after slice (no
  whole-`ShaderStruct` snapshot — keeps history and project files small):
  `SetShaderCodeOp` (before/after GLSL text of one stage), `SetShaderFieldOp`
  (before/after of one whole IR element — a `ShaderFieldValue` variant, keyed by
  section + field index), and `AddShaderFieldOp` (append vs. remove one default entry via a `bool add`
  flag, whose `Inverse()` flips the flag). All are keyed by mesh UID + stage.
  Code edits are gesture-bounded: `ShaderEditBegin` snapshots `m_beforeCode`,
  `ShaderCodeEdited` applies live, `ShaderEditEnd` records one `SetShaderCodeOp`
  on focus-out only if the code changed (no net change → no op). Discrete
  struct/field edits have no gesture: `ShaderStructEdited` snapshots the element,
  applies the UI's `{field,value}` via `VisitElement`/`ApplyFieldEdit`, and records
  a `SetShaderFieldOp` only if the element changed (`before != after`); `ShaderFieldAdded` calls `AppendDefault` and
  records an `AddShaderFieldOp(add=true)`. Undo/redo replays four dedicated
  restore events — `ShaderCodeRestored`, `ShaderFieldRestored`,
  `ShaderFieldAddRestored`, `ShaderFieldRemoved` — distinct from the forward
  events so the replay handlers bump `ShaderUnit::m_version` (panel refresh)
  while live forward edits do NOT (avoids cursor-jump mid-typing). Restore is
  **CPU-only, no recompile to SPIR-V**; the user presses Compile to push
  restored source to the GPU. All three ops are deliberately non-mergeable
  (empty `MergeKey`). Shader Create is undoable via `ShaderLinkOp` (recorded by
  the Editor; a pool-preserving membership toggle - see
  operation-system.instructions.md); Compile stays a non-undoable lifecycle
  action.

