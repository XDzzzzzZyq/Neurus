# Event System

## Overview

Neurus uses a **two-tier event system** to enforce layer isolation. UI panels
emit Qt signals; a template bridge in Application converts these to typed
event structs; Editor subscribes and handles them through an internal
EventQueue. The result: panels never include Editor headers, Editor has zero
Qt dependencies, and all cross-layer wiring is auditable in one file.

```
Panel Qt signal (e.g. Outliner::objectClicked - pure input intent)
  └── Application::ConnectUIEvent<Outliner, ObjectClicked>
        └── [lambda calls Editor::OnUIEvent(e)]
              └── EventQueue::enqueue<ObjectClicked>
                    └── EventQueue::Process() → Editor wraps to ObjectSelected, then subscriber handles it
```

**Ephemeral Events - the big idea of the event system**

Every event in Neurus is ephemeral: enqueued and processed within a single
frame, then destroyed once `EventQueue::Process()` returns. Because events live
only as long as the objects they reference, they can safely carry raw pointers
(`const UID*`, `Camera*`, and future `Shader*`, ...) instead of integer IDs.
`UID` is the common base of `Scene` and `ObjectID`, so a single `const UID*`
payload covers both the Editor-owned Scene (selection state) and the scene
objects being mutated. Controllers therefore never re-fetch objects from the
Scene by ID - the pointer IS the object. Controllers cast a `const UID*` back
to the concrete type via the templated `ObjectID::As<T>()` (which compares
`o_type` against `T::Type` and returns nullptr on mismatch) and the untyped
`ObjectID::As()`, plus `Scene::As` for scene refs. The UI layer emits PURE
INPUT INTENTS (mouse events, object clicks, delete requests); the Editor wraps
them into complete scene events, stamping the scene it owns. This ephemerality
is what makes pointer payloads safe, and it applies to the whole event system,
not just scene events.

### Three event paths

Cross-layer traffic flows along three distinct paths, all still "UI emits,
Editor wraps or forwards, controllers handle":

1. **UI -> Editor pure intents** (`InputEvents.h`): the raw input stream -
   mouse move/press/release/scroll plus `ObjectClicked { const UID* object; int modifiers; }`
   and `DeleteRequested { }`. These carry no scene pointer; the UI does not
   hold or stamp the scene (`m_scene` is gone from the panels).
2. **UI -> Controller editing events** (`SceneEvents.h`, `ShaderEvents.h`):
   complete scene mutations (selection, transform, visibility, property edits,
   shader edits) carrying `const UID* object`. Panels emit these with the active
   object's UID base pointer; `Editor::OnUIEvent` enqueues them unchanged and
   controllers cast back via `ObjectID::As<T>` / the untyped `ObjectID::As`.
3. **Editor -> Controller wrapped events**: `ObjectSelected` (the
   Editor subscribes to `ObjectClicked` and forwards
   `ObjectSelected{ m_scene.get(), object, modifiers }`, and the Application
   emits it for the viewport IDBuffer pick), `ObjectDeleteRequested` (the
   Editor subscribes to `DeleteRequested` and forwards
   `ObjectDeleteRequested{ m_scene.get() }`), the membership events
   (`SceneObjectAddRequested` / `SceneObjectDeleteRequested`), the replay /
   restore events, and `CameraEvents` which still carry `Camera*`.

## Location

| File | Purpose |
|------|---------|
| `src/editor/events/SceneEvents.h` | Scene-domain events (selection, transform, visibility, camera/mesh/light/env property changes) - ephemeral, carry `const UID*` |
| `src/editor/events/EditorEvents.h` | Cross-component events only (RenderResetEvent, EnvironmentChanged, SceneModified, LightGpuChanged, LightingRebuild, SceneObjectGpuUploadRequested) — pure data, no Qt |
| `src/editor/events/CameraEvents.h` | Camera input events (rotate, push, slide, zoom, resize) |
| `src/editor/events/InputEvents.h` | Viewport raw input events (mouse move, press, release, scroll) |
| `src/editor/events/ProjectEvents.h` | Project lifecycle events (new, open, save, saveAs) |
| `src/editor/events/AssetEvents.h` | Asset import events (mesh, camera, light add) |
| `src/editor/events/OperationEvents.h` | Undo/redo request events (`UndoRequested`, `RedoRequested`) |
| `src/editor/events/ConfigEvents.h` | Render config change events |
| `src/editor/events/ShaderEvents.h` | Shader editor events (create, compile, code/struct edit, field add) |
| `src/editor/events/UIEvents.h` | QObject singleton — Qt signal definitions for UI→Editor dispatch |
| `src/editor/events/EventBus.h` | Header-only typed event dispatcher (`EventQueue` class, zero Qt dependency) |

## Event Structs (Pure Data)

All event structs live in `src/editor/events/` and carry no Qt headers.
They are plain C++ structs passed by const reference through the system.

### SceneEvents.h

Scene-domain events carry POINTERS (`const UID*` object, `const UID*` scene)
instead of integer IDs. `UID` is the common base of `Scene` and `ObjectID`, so
both payloads erase to the same type. Controllers cast these to the concrete
type (Mesh*, Light*, Camera*, Environment*) via `ObjectID::As<T>` / the untyped
`ObjectID::As` in the .cpp and mutate them directly - no Scene lookup by ID.

```cpp
// Selection
struct ObjectSelected {
    const UID*   scene  = nullptr;  // Editor-owned Scene (selection state)
    const UID* object = nullptr;  // Selected object (nullptr = background click)
    int   modifiers = 0;            // Input::Modifiers bitmask
};

struct ObjectDeselected {
    const UID*   scene  = nullptr;
    const UID* object = nullptr;
};

// Selection replay (dispatched only by SetSelectionOp on undo/redo): absolute
// selection-set restore by UID. Live selection uses the incremental events above.
struct SelectionChanged {
    const UID*       scene = nullptr;   // Editor-owned Scene (selection state)
    std::vector<int> selectedUids;      // Ordered selected object UIDs
    int              activeUid = 0;     // Active object UID (0 = none)
};

// Visibility
struct VisibilityChanged {
    const UID* object = nullptr;
    bool viewportVisible = true;
    bool renderVisible   = true;
};

// Transform
struct PositionChanged { const UID* object; float posX, posY, posZ; };
struct RotationChanged { const UID* object; float rotX, rotY, rotZ; };
struct ScaleChanged    { const UID* object; float sclX, sclY, sclZ; };

// Camera properties
struct CameraTargetChanged { const UID* object; float targetX, targetY, targetZ; };
struct CameraFovChanged    { const UID* object; float fov; };
// Absolute camera pose (position + target) — replay only, dispatched by
// CameraTransformOp on undo/redo (live navigation carries relative deltas).
struct CameraPoseChanged   { const UID* object; float posX, posY, posZ, tarX, tarY, tarZ; };

// Mesh properties
struct MeshShadowChanged  { const UID* object; bool enabled; };
struct MeshMaterialChanged { const UID* object; bool enabled; };

// Light properties
struct LightPowerChanged      { const UID* object; float power; };
struct LightRadiusChanged     { const UID* object; float radius; };
struct LightShadowChanged     { const UID* object; bool enabled; };
struct LightCutoffChanged     { const UID* object; float cutoff; };
struct LightOuterCutoffChanged { const UID* object; float outerCutoff; };

// Environment properties
struct EnvironmentIntensityChanged { const UID* object; float intensity; };
struct EnvironmentRotationChanged  { const UID* object; float rotation; };

// Scene membership (Add / Delete) — UID-carrying (replay-safe; see below)
struct SceneObjectAddRequested { const UID* scene; int objectUid; };     // forward (Editor import) + replay
struct SceneObjectDeleteRequested { const UID* scene; std::vector<int> uids; };  // single BATCHED removal path (gesture + replay)
struct ObjectDeleteRequested { const UID* scene; };                      // Editor-wrapped Delete gesture, FORWARD-ONLY (Editor stamps scene)
```

**Scene Events Are Ephemeral**

Scene-domain events follow the system-wide ephemeral rule (see Overview above):
they carry pointers, never integer IDs. Controllers cast the event's `const UID*`
to the concrete type via `ObjectID::As<T>` (comparing `o_type` against `T::Type`)
or the untyped `ObjectID::As` and mutate the object directly - no Scene lookup
by ID. `Editor::OnUIEvent` just enqueues them unchanged.

**Membership events are the exception: they carry UIDs.** Add/Delete is split
into **Import** (Editor: `Load` the resource into the pool — `OnMeshImport`,
`OnCameraAdd`, `OnLightAdd`, ...) and **Add** (SceneController: fetch the
pooled object by UID and register it). The membership events carry the object
UID instead of a pointer because they double as replay events for the
`SceneObjectAddOp` undo operation (see operation-system.instructions.md): a
serialized op replays by UID, and the pool is the single owner of the object.
`SceneObjectAddOp` re-dispatches the originating add/delete events on replay,
so undo/redo runs the same controller handlers as live edits; the handler's
`Submit` is muted by `OperationManager`'s `Phase::Replaying` guard, so
playback does not re-record (the expected "Submit suppressed during replay"
LOG).
`ObjectDeleteRequested` is the Editor-wrapped Delete gesture: the Editor
subscribes to the pure `DeleteRequested` intent (Delete key in
Outliner/Viewport) and forwards `ObjectDeleteRequested{ m_scene.get() }` -
the UI no longer stamps the scene. It is **forward-only** (the recorded
composite replays via `SceneObjectDeleteRequested`, never this gesture
event). The SceneController snapshots the selection, guards the last
camera, deselects, and DEFERS the actual removals as ONE batched
`SceneObjectDeleteRequested` carrying all selected UIDs — so the batched
handler is the SINGLE removal path shared by the gesture and by undo/redo
replay (no replay-only handling), and records ONE light composite
(`CompositeOp[SetSelectionOp(before→∅), SceneObjectAddOp(uids,false)]` — a
delete of N objects is ONE op, not N). The forward add path records
`CompositeOp[SceneObjectAddOp({u},true), SetSelectionOp(before→{u})]` — add
AND select collapse to one undo entry.

### EditorEvents.h

Cross-component events only — things that involve a DIFFERENT component than
the emitter (SceneController -> Editor, Editor -> Renderer). Scene-domain events
live in SceneEvents.h.

```cpp
// Cross-component events (SceneController -> Editor, Editor -> Renderer)
struct SceneModified {};                       // mark project dirty
struct LightGpuChanged { const UID* object; };  // single light SSBO update
struct LightingRebuild {};                     // full light SSBO dict rebuild
struct SceneObjectGpuUploadRequested { const UID* object; };  // on-demand MeshGPU/LightGPU/EnvironmentGPU upload (scene add + shadow toggle)
struct RenderResetEvent {};                    // temporal accumulation reset
```

### CameraEvents.h

```cpp
struct CameraRotateEvent  { Camera* cam; float mouse_delta_x, mouse_delta_y; };
struct CameraZoomEvent    { Camera* cam; float scroll_dir; };
struct CameraPushEvent    { Camera* cam; float mouse_delta_x, mouse_delta_y; };
struct CameraSlideEvent   { Camera* cam; float mouse_delta_x, mouse_delta_y; };
struct CameraSpinEvent    { Camera* cam; float mouse_delta_x, mouse_delta_y; };
struct CameraResizeEvent  { Camera* cam; int width; int height; };

// Drag-gesture boundaries — bracket a continuous orbit/pan/dolly manipulation
// so it collapses to ONE undo entry (controller-owned gesture, see
// editor.instructions.md → Undo/Redo). CameraController captures the "before"
// pose on begin, mutates live during the drag WITHOUT recording, and records
// one CameraTransformOp on end.
struct CameraDragBegin { Camera* cam; };
struct CameraDragEnd   { Camera* cam; };
```

Scroll zoom has no press/release, so it is NOT bracketed: `CameraZoomEvent`
records per-event and relies on `CameraTransformOp::MergeKey()` to coalesce a
scroll burst into one undo step.

### InputEvents.h

```cpp
struct MouseMoveEvent   { int x, y; float dx, dy; Modifiers mods; };
struct MousePressEvent  { int x, y; MouseButton btn; Modifiers mods; };
struct MouseReleaseEvent{ int x, y; MouseButton btn; Modifiers mods; };
struct MouseScrollEvent { int x, y; float delta; Modifiers mods; };

// Pure UI->Editor intents (no scene pointer - the Editor wraps them):
struct ObjectClicked    { const UID* object; int modifiers; };  // Outliner row click
struct DeleteRequested  { };                                     // Delete key (Outliner/Viewport)
```

### ConfigEvents.h

```cpp
// Whole-config change (UI panel emits it; SetRenderConfigOp replays it).
struct RenderConfigChangedEvent { RenderConfig config; };

// Slider-drag boundaries — same controller-owned gesture pattern as the camera
// drag (see editor.instructions.md → Undo/Redo). RenderConfigController captures
// the "before" config on begin, applies intermediate values live WITHOUT
// recording, and records one SetRenderConfigOp on end. Discrete edits (checkbox,
// combo box) arrive with no gesture and are recorded immediately, one op each.
struct ConfigEditBegin {};
struct ConfigEditEnd   {};
```

### ShaderEvents.h

Events for the shader editor pipeline (UI → Editor → ShaderController). All events
carry a `const UID*` resolved once by the ShaderEditorPanel from the active
scene selection. `ShaderSection` identifies which `ShaderStruct` container is edited.

```cpp
enum class ShaderSection : int {
    Attributes = 0, PassOutputs = 1, Inputs = 2, Outputs = 3,
    Uniforms = 4, StructDefs = 5, Functions = 6, PushConstants = 7
};

struct ShaderCreateRequested  { const UID* object; };
struct ShaderCompileRequested { const UID* object; int stage; int unitType; };  // 0=Code path, 1=Struct path
struct ShaderCodeEdited       { const UID* object; int stage; std::string code; };
struct ShaderStructEdited     { const UID* object; int stage; ShaderSection section;
                                int fieldIndex; int subFieldIndex; std::string field; std::string value; };
struct ShaderFieldAdded       { const UID* object; int stage; ShaderSection section; int subFieldIndex; };

// Undo/redo wiring (content edits only — see operation-system.instructions.md)
struct ShaderEditBegin        { const UID* object; int stage; };  // gesture start: snapshot "before" code
struct ShaderEditEnd          { const UID* object; int stage; };  // gesture end: record one SetShaderCodeOp if changed
// Replayed restore events (mirror the forward edit events 1:1, but bump version)
struct ShaderCodeRestored     { const UID* object; int stage; std::string code; };
struct ShaderFieldRestored    { const UID* object; int stage; ShaderSection section;
                                int fieldIndex; ShaderFieldValue value; };  // whole element to assign back
struct ShaderFieldAddRestored { const UID* object; int stage; ShaderSection section; int subFieldIndex; };  // redo of an add
struct ShaderFieldRemoved     { const UID* object; int stage; ShaderSection section; int subFieldIndex; };  // undo of an add
```

**Version flow:** Only `ShaderCreateRequested` and `ShaderCompileRequested` bump
`Shader::m_version` (and only on successful compile). The other events mutate
data without recompiling — the user must press Compile to apply changes to the
GPU pipeline. Because create/compile rebuild the pipeline, `ShaderController`
enqueues `RenderResetEvent` after both, so temporal accumulation resets.

**Undo/redo flow:** Content edits are recorded as delta-only ops matching each
edit event: `SetShaderCodeOp` (before/after code), `SetShaderFieldOp`
(before/after of one whole IR element — a `ShaderFieldValue` variant over
`S_IO`/`S_Uniform`/`S_Func`/`S_PushConstant`/`S_StructDef`) and `AddShaderFieldOp`
(append vs remove one default entry). The forward `ShaderStructEdited` still
delivers a single `{subFieldIndex, field, value}`; the controller applies it onto
the live element and snapshots the element before/after for the op. Code edits are
bracketed by `ShaderEditBegin`/`ShaderEditEnd` so a keystroke burst collapses to
one undo entry on focus-out; discrete struct/field edits record one op each.
Undo/redo replays a dedicated restore event
(`ShaderCodeRestored` / `ShaderFieldRestored` / `ShaderFieldAddRestored` /
`ShaderFieldRemoved`) that re-applies one edit dimension and bumps
`ShaderUnit::m_version` (panel refresh) — CPU-only, no recompile to SPIR-V.
Create/Compile stay non-undoable. See
[operation-system.instructions.md](operation-system.instructions.md).

## UIEvents (Qt Signal Bus)

A QObject singleton providing typed Qt signals. Panels connect to these
signals directly; Editor never touches them — the bridge is in Application.

```cpp
class UIEvents : public QObject {
    Q_OBJECT
public:
    static UIEvents& instance();
};

// Profiling is always collected while the app runs: DeferredRenderer populates
// a FrameProfile every frame (no toggle) and DrawFrame() returns it. Application
// assembles the UIContext each frame (Editor never produces it), setting
// UIContext::profile to the returned FrameProfile (no UIEvents signal, no Editor
// copy).
//
// Panel signals (defined on the panel itself, not UIEvents):
//   Outliner::objectClicked(const ObjectClicked& e)
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
eventBus.subscribe<LightGpuChanged>([](const LightGpuChanged& e) {
    uploadSingleLight(ObjectID::As<Light>(e.object));
});

// Enqueue events (deferred dispatch)
eventBus.enqueue(LightGpuChanged{lightPtr});

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
    void objectClicked(const ObjectClicked& e);
    void visibilityChanged(const VisibilityChanged& e);
    void deleteRequested(const DeleteRequested& e);
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
ConnectUIEvent(outliner, &Outliner::objectClicked);
ConnectUIEvent(outliner, &Outliner::visibilityChanged);
ConnectUIEvent(outliner, &Outliner::deleteRequested);
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

### Step 4 — Controller subscribes

```cpp
// SceneController::Init() — handles scene-domain events via free-function handlers
bus.subscribe<ObjectSelected>([&bus](const ObjectSelected& e) {
    OnObjectSelected(e, bus);
});
bus.subscribe<VisibilityChanged>([&bus](const VisibilityChanged& e) {
    OnVisibilityChanged(e, bus);
});

// Editor::Initialize() — handles cross-component GPU-sync events
ed_eventBus.subscribe<LightGpuChanged>([this](const LightGpuChanged& e) {
    auto* light = ObjectID::As<Light>(e.object);
    if (!light) return;
    auto gpuStruct = ed_uploadManager->UploadLighting(*light);
    ed_renderer->GetRenderCache().UpdateLight(e.object->GetObjectID(), gpuStruct);
});
ed_eventBus.subscribe<LightingRebuild>([this](const LightingRebuild&) {
    UploadLighting();
});
ed_eventBus.subscribe<SceneModified>([this](const SceneModified&) {
    m_dirty = true;
});
```

## Data Flow Summary

```
User Input (QML / Qt widgets)
    │
    ▼
UI Panel (Qt signal, e.g. Outliner::objectClicked - pure intent)
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
SceneController / Editor handlers ← business logic
    ├── Select / Deselect (SceneController)
    ├── SetVisible / transform setters (SceneController)
    └── SetRenderConfig / GPU uploads (Editor)
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
Scene mutations are handled by `SceneController`, which enqueues
`RenderResetEvent{}` via the shared `Mutated()` helper after every mutation.

**Subscribers** listen for this event to reset per-frame history:
- `DeferredRenderer::ResetShadowAccumulation()` — zeros the shadow accumulation iteration counter.
- Future: SSAO temporal accumulation, SSR temporal accumulation, TAA jitter reset.

### SceneController GPU-Sync Flow

Scene mutations that affect GPU resources follow a three-tier event pattern:

```
SceneController mutation handler
  ├── Mutates the scene object directly (via ObjectID::As<T> from the event's const UID*)
  └── Enqueues one or more EditorEvents:
        ├── SceneModified{} → Editor marks project dirty
        ├── RenderResetEvent{} → DeferredRenderer resets temporal accumulation
        │
        ├── Scene object enters scene (add / undo-redo re-add) or light
        │   shadow toggled ON → SceneObjectGpuUploadRequested{object}
        │     └── Editor::OnSceneObjectGpuUpload handler:
        │         casts to Mesh*/Light*/Environment* and uploads on demand
        │         (MeshGPU / LightGPU shadow maps / IBL), skip-if-cached
        │
        ├── Light property change → LightGpuChanged{lightPtr}
        │     └── Editor::LightGpuChanged handler:
        │         casts to Light*, calls UploadLighting(*light),
        │         calls RenderCache::UpdateLight(id, gpuStruct)
        │         → single SSBO struct updated
        │
        └── Light transform/visibility/shadow toggle → LightingRebuild{}
              └── Editor::LightingRebuild handler:
                  calls UploadLighting() → full light SSBO dict rebuilt
                  → all light SSBOs re-uploaded
```

The key invariant: **SceneController never touches GPU resources**. It mutates
scene objects and emits EditorEvents. Editor handles all GPU uploads in its
event handlers. This keeps the controller stateless and preserves the Editor→Renderer GPU upload boundary.
