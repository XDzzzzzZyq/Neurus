# Operation System (Undo/Redo)

## Overview

Event-replay undo/redo (Design B, group-theoretic), implemented in
`src/editor/operations/`. Operations never mutate the scene directly: `Apply()`
re-dispatches the originating scene event via `EventQueue::emitNow`, so the
existing controller handler performs the mutation. This keeps a **single
mutation path** — undo/redo exercises the same code as a live edit. A
`Phase::Replaying` guard in `OperationManager` makes `Submit()` a no-op during
replay so playback does not re-record. The guard is RAII (restored on
exception), `Replay()` catches + logs handler exceptions and returns whether
the op applied, and `Undo()`/`Redo()` only move an op onto the opposite stack
when replay succeeded — a failed replay is dropped, never enqueued as a phantom
entry.

The controller-side wiring that produces operations (gesture begin/end,
per-controller specifics) lives in
[editor.instructions.md](editor.instructions.md); this doc covers the operation
model, coalescing rules, and persistence.

## Operation model

- Operations are **absolute state-sets** (before/after endpoints by UID), not
  deltas — safe to replay across intervening changes and to no-op on stale UIDs.
- `TransitionOp<Derived, TEvent, Value>` (CRTP) covers per-object value edits;
  `Inverse()` swaps before/after. `MergeKey()`/`MergeFrom()` coalesce a
  continuous manipulation (e.g. camera drag) into one undo entry.
- `Operation::PreservesRedo()` (default false): a branching edit clears the redo
  stack. **Selection** ops (`SetSelectionOp`) override it to `true` so navigating
  the selection appends to undo *without* discarding a pending redo — safe
  because ops are absolute. Selection is scene-level SET state, so it replays via
  the absolute `SelectionChanged` event + `Selections::RestoreState`. The *live*
  selection is also persisted independently of history by `Scene::serialize`
  (as UIDs — see data-resource.instructions.md), so a reopened project restores
  its selection even with an empty undo/redo history.

## Bounded undo depth

`OperationManager` caps the undo stack at `kDefaultMaxUndoDepth` (256) entries;
the ctor takes an optional `maxUndoDepth` override (0 = unbounded). `Submit` and
`RestoreHistory` call `EnforceUndoLimit()`, which evicts the **oldest** entries
(front of `m_undo`) once the cap is exceeded, bounding memory over long sessions.
Redo is bounded implicitly: its entries only ever originate from undo pops.

## Coalescing gestures into one undo entry

A continuous manipulation (a slider drag, a camera orbit) fires a *stream* of
value changes but must collapse to a single undo entry. Two strategies exist:

- **Implicit merge (MergeKey):** the op declares a non-empty `MergeKey()`;
  `OperationManager::Submit` folds a same-key edit into the undo-stack top.
  Used where there is no natural press/release boundary — **scroll zoom**
  (`CameraZoomOp`, keyed `camera_zoom:<uid>`) records per-event and merges.
- **Controller-owned gesture (explicit begin/end):** the controller holds a
  small gesture state, captures the "before" endpoint on a begin event, mutates
  live during the drag WITHOUT recording, and records ONE op on the end event.
  Used where the UI has a real press/release boundary. This needs NO changes to
  `OperationManager` or `IOperationSink` — the boundaries are ordinary typed
  events flowing through the same controller chain.

For the concrete controller wiring (`CameraController`,
`RenderConfigController`, `ShaderController`), see
[editor.instructions.md](editor.instructions.md).

`ShaderController` records delta-only ops matching each edit event's
granularity: `SetShaderCodeOp` (before/after GLSL text), `SetShaderFieldOp`
(before/after of one whole `ShaderStruct` element — a `ShaderFieldValue` variant
over `S_IO`/`S_Uniform`/`S_Func`/`S_PushConstant`/`S_StructDef`, serialized via the
non-intrusive functions in `render/shaders/ShaderStructSerialize.h`) and
`AddShaderFieldOp` (append vs remove one default entry, keyed by a `bool add` flag
it flips in `Inverse`).
All are keyed by mesh UID + stage. Code edits use the explicit begin/end gesture
(`ShaderEditBegin`/`ShaderEditEnd`), while discrete struct/field edits record one
op immediately per change. The ops are deliberately non-mergeable (empty
`MergeKey`). Their `Apply`/replay is CPU-only — each re-emits a dedicated restore
event (`ShaderCodeRestored` / `ShaderFieldRestored` / `ShaderFieldAddRestored` /
`ShaderFieldRemoved`) that re-applies one edit dimension and bumps
`ShaderUnit::m_version`, never recompiling to SPIR-V.

## Persisting the history stacks

The undo/redo stacks are saved into the project file via `HistoryComponent`
(`src/asset/components/HistoryComponent.h/cpp`), a `project::Serializable`
adapter keyed `"m_history"` that wraps `OperationManager`. `Save` writes the
`undo`/`redo` vectors of `std::unique_ptr<Operation>`; `Load` decodes them and
calls `OperationManager::RestoreHistory`. `Application::BuildProject` registers
it **last**, after Scene/Config/UI, so a legacy file with no `m_history` node
still loads — `HistoryComponent::Load` catches the cereal exception and clears
the stacks rather than throwing.

Operations serialize polymorphically through cereal, mirroring scene objects
(`src/scene/registrations/TypeRegistration.cpp`). Each op has a default ctor and
a templated `serialize`; concrete types are registered in
`src/editor/operations/registrations/OperationRegistration.cpp` via
`CEREAL_REGISTER_TYPE` +
`CEREAL_REGISTER_POLYMORPHIC_RELATION(neurus::Operation, …)`. Two non-obvious
requirements when adding a new op to that file:

- The registration TU **must** `#include <cereal/archives/json.hpp>` before the
  macros, otherwise the type binds to no archive and save throws "Trying to save
  an unregistered polymorphic type" at runtime despite compiling cleanly.
- Ops live in a static lib, so the registration TU is dead-stripped unless
  force-linked: `CEREAL_REGISTER_DYNAMIC_INIT(neurus_operations)` in the `.cpp`
  plus `CEREAL_FORCE_DYNAMIC_INIT(neurus_operations)` (in
  `operations/registrations/OperationRegistration.h`) included at every
  serialization site.

> **Why no `polymorphic_name` on scene objects but yes on ops?** cereal only
> writes a type name when a pointer's runtime type differs from its declared
> static type. Scene pools hold concrete-typed pointers (`shared_ptr<Camera>`,
> etc.), so cereal takes the exact-type fast path (`polymorphic_id` sentinel
> `0x40000000`, no name). Operation stacks hold `unique_ptr<Operation>` (abstract
> base) with derived runtime types, so cereal records `polymorphic_name` for
> dispatch on load.
>
> Because scene pools are concrete-typed, `TypeRegistration.cpp` is never
> odr-used and is currently dropped from the final binaries by the static
> linker — the project round-trip tests pass without it. It is kept as the
> registration reference for scene types; if scene objects are ever serialized
> through `shared_ptr<ObjectID>` base pointers, it will need the same
> `CEREAL_REGISTER_DYNAMIC_INIT`/`CEREAL_FORCE_DYNAMIC_INIT` treatment as the
> ops.

## Adding a new operation

1. Give the op a default ctor + templated `serialize`.
2. Add the two registration lines in `operations/registrations/OperationRegistration.cpp`
   (`CEREAL_REGISTER_TYPE` + `CEREAL_REGISTER_POLYMORPHIC_RELATION`).

No manual type-tag or factory is needed.
