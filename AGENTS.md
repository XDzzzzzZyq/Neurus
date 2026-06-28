# AGENTS.md
# Guidance for agentic coding in this repo

This repository is a C++20 Vulkan-HPP 1.4 real-time renderer with strict layer
isolation. Use this file as the high-level reference; detailed rules live in
`.github/instructions/*.md`.

---

## Quick Reference

| Topic | Document |
|-------|----------|
| Architecture & Layers | [architecture.instructions.md](.github/instructions/architecture.instructions.md) |
| Build & Test | [build.instructions.md](.github/instructions/build.instructions.md) |
| Code Style & Error Handling | [style.instructions.md](.github/instructions/style.instructions.md) |
| Git Workflow | [git-workflow.instructions.md](.github/instructions/git-workflow.instructions.md) |
| Testing Standards | [test.instructions.md](.github/instructions/test.instructions.md) |
| Renderer Layer | [renderer.instructions.md](.github/instructions/renderer.instructions.md) |
| Editor Layer | [editor.instructions.md](.github/instructions/editor.instructions.md) |
| UI System | [ui-system.instructions.md](.github/instructions/ui-system.instructions.md) |
| Asset Layer | [data-resource.instructions.md](.github/instructions/data-resource.instructions.md) |

---

## Architecture (Hard Requirements)

```
┌──────────────────────────────────────────────────────────────┐
│ UI Layer (Qt6 Widgets + ADS)                                 │
│  owns: VkSurfaceKHR, QWindow, UIEvents (QObject singleton)   │
│  QML provides window + input ONLY. No rendering logic.      │
└─────────────────────┬────────────────────────────────────────┘
                      │ Qt Signals/Slots
                      ▼
┌──────────────────────────────────────────────────────────────┐
│ Editor Layer                                                │
│  owns: Controllers, SelectionManager, EditorContext         │
│  Application logic, scene mutation, event orchestration     │
└─────────────────────┬────────────────────────────────────────┘
                      │
            ┌─────────┴──────────┐
            ▼                    ▼
┌──────────────────┐  ┌────────────────────────────────────────┐
│ Data & Resource   │  │ Renderer Layer (Vulkan-HPP vk::raii)   │
│  owns: allocators │  │  owns: VkInstance, VkDevice, VkQueue,  │
│  descriptor pools │  │   VkSwapchainKHR, VkPipeline,          │
│  pipeline cache   │  │   VkCommandBuffer, all GPU resources   │
│  (stub for MVP)   │  │  consumes: VkSurfaceKHR (read-only)    │
└──────────────────┘  └────────────────────────────────────────┘
```

### Isolation Chain (Hard Requirement)

Data and control flow follow this strict unidirectional chain:

```
UI Layer (Qt6) → UIEvents (Qt Signals) → Editor → EventQueue (typed events) → Controllers → Context → Renderer
```

- UI emits signals via UIEvents. NEVER calls the Editor or Renderer directly.
- Editor receives raw InputState, translates to typed events via EventQueue, dispatches to Controllers.
- Controllers handle discrete events (CameraController subscribes to CameraEvents) -- no per-frame polling.
- Renderer receives read-only scene data through Context objects. NEVER calls back into Editor.
- Each arrow represents a **decoupled interface** (signals, events, contexts). No layer knows the internals of the next.

### Layer Boundaries

- **Renderer**: pure rendering service; owns GPU resources; consumes read-only
  scene data; must not mutate application-level state.
- **Editor**: application logic and scene mutation; owns Controllers;
  communicates via Context, UIEvents (Qt signals), and EventQueue (typed events).
- **UI**: Qt6 Widgets presentation only; owns surface; emits signals.
- **Data & Resource**: GPU resource management; descriptor pools, allocators,
  buffer/image abstractions. (Stub for MVP.)

### Communication

Cross-layer communication MUST go through:
- **UIEvents** (Qt Signals/Slots) for UI↔Editor signals
- **EventQueue** for Editor↔Renderer events
- **Context objects** for data queries

Direct coupling across layers is forbidden. Renderer must not include Editor or
UI headers. UI must not include Renderer headers (only surface handle).

### Vulkan Ownership

- UI Layer: owns `VkSurfaceKHR` (created from QWindow + QVulkanInstance)
- Renderer Layer: owns everything else (instance via QVulkanInstance sharing,
  device, swapchain, pipeline, command pool, all GPU resources)
- No shared ownership of GPU resources across layers.
- Non-copyable GPU resource classes (`= delete` copy/assign).

### Threading

- Main thread only for MVP.
- Future: Renderer may run on dedicated thread with proper synchronization.

---

## Development Guidelines

Follow Karpathy Guidelines. For each task:

1. Think and plan. System design should be highly decoupled and elegant.
2. Implement. Do not hide errors, expose them directly. Use debug printing and
   logging. Wire every feature **IMMEDIATELY** into the renderer and program.
3. Test and Verify. 
   - The design of test should be comprehensive (mathematical verification, reference image test, etc.), forming a logic chain that proves the correctness of each component.
      - Reference image must be verified externally through python: The generated reference is not empty (transparent), purely black or write, or any unreasonable result. Do not cheat yourself.
   - Do not just run unit tests -- **always launch `Neurus.exe`** to check:
      - Terminal output for validation errors (`VUID-...`), crashes, or unexpected log messages
      - Visual correctness in the rendered viewport (use screenshots for analysis)
      - Runtime behavior: resize the window, interact with the viewport, verify no deadlocks or freezes.
   - Do not move to the next stage if the test failed
   
4. Before committing, keep ALL relevant documents and guidances (`.github/instructions/*.md`) updated. No validation error, no unreasonable reference image, all tests passed.
5. Commit. Follow the current commit format. Moreover, in the description, add "No validation error, no unreasonable reference image, all tests passed."

After each development phase, stop and wait for user verification.

---

## File Layout

```
Neurus/
├── .github/
│   ├── instructions/       # Architecture/component docs for AI agents
│   └── workflows/          # GitHub Actions CI
├── cmake/                  # CMake helper modules
├── dep/                    # Git submodule dependencies
│   └── qtadvanceddocking/  # Qt-Advanced-Docking-System (ADS)
├── res/shaders/            # GLSL shader source files
├── src/
│   ├── render/             # Renderer layer (Vulkan-HPP)
│   │   ├── Barrier.h/cpp            # Centralized image barrier management
│   │   ├── DeferredRenderer.h/cpp   # Deferred PBR pipeline (active renderer)
│   │   ├── Image.h/cpp              # GPU image with state tracking (ImageState)
│   │   ├── ShaderProgram.h/cpp
│   │   ├── Swapchain.h/cpp
│   │   ├── VulkanContext.h/cpp
│   │   └── passes/          # Render passes
│   │       ├── RenderCache.h/cpp     # Cross-frame mutable resource pool
│   │       ├── RenderContext.h       # Per-frame immutable scene snapshot
│   │       ├── SyncObjects.h/cpp     # Fence, Semaphore, FrameSync, BufferBarrier
│   │       ├── GeometryPass.h/cpp
│   │       ├── SSAOPass.h/cpp
│   │       ├── LightingPass.h/cpp
│   │       ├── IBLPass.h/cpp
│   │       └── ShadowDepthPass.h/cpp
│   │   └── buffers/          # Buffer class hierarchy
│   │       ├── Buffer.h/cpp         # Virtual base class (Buffer)
│   │       ├── StagingBuffer.h/cpp  # Host-visible staging
│   │       ├── GPUBuffer.h/cpp      # Device-local with staging
│   │       ├── UniformBuffer.h      # Template uniform (UniformBuffer<T>)
│   │       ├── VertexBuffer.h/cpp   # Vertex buffer (inherits GPUBuffer)
│   │       ├── IndexBuffer.h/cpp    # Index buffer (inherits GPUBuffer)
│   │       └── BufferLayout.h/cpp   # Vertex input layout description
│   ├── editor/             # Editor layer (logic, controllers)
│   │   ├── events/          # Event system (UIEvents + typed EventQueue)
│   │   │   ├── UIEvents.h/cpp    # Qt signal bus for UI↔Editor
│   │   │   ├── EventQueue.h        # Typed EventQueue dispatcher (no Qt)
│   │   │   └── CameraEvents.h    # Camera event structs
│   │   ├── controllers/     # Controller implementations
│   │   │   ├── Controllers.h     # Base class for all controllers
│   │   │   ├── CameraController.h/cpp  # Event-driven camera controls
│   │   ├── EditorContext.h/cpp
│   │   └── CMakeLists.txt
│   ├── ui/                 # UI layer (Qt6 Widgets + ADS)
│   │   ├── NeurusMainWindow.h/cpp # Main window with ADS dock manager
│   │   ├── MainWindow.h/cpp       # (legacy QWindow subclass)
│   │   ├── VulkanWidget.h/cpp     # (legacy vk::raii widget)
│   │   └── qml/            # QML source files (legacy)
│   ├── data/               # Data & Resource layer
│   └── main.cpp            # Application entry point
├── test/
│   ├── render/             # Renderer GPU tests
│   │   └── reference/      # Reference images for regression tests
│   │       └── deferred/   # Deferred-pass reference PNGs
│   ├── editor/             # Editor unit tests (run in CI, no GPU)
│   └── shared/             # Test infrastructure
│       └── TestVulkanShared.h/cpp  # GPU test fixture base class
├── AGENTS.md               # This file
├── CMakeLists.txt           # Root CMake build
├── CMakePresets.json        # CMake presets (default, release, vs2022)
├── Makefile                 # Convenience build wrapper
└── README.md               # Project introduction
```

---

## Practical Guidance

- Respect layer isolation; do not introduce cross-layer header includes where
  forbidden.
- Prefer UIEvents/EventQueue/Context-driven flows over direct calls across layers.
- Every cross-layer interaction MUST go through the isolation chain
  (UIEvents/EventQueue/Context) -- no shortcuts. If a function reaches
  across layers by including a forbidden header or calling a method on an
  object it doesn't own, that's a design violation.
- Use the event-driven pattern (Controller subscribes, Editor dispatches)
  instead of polling or Update() loops. Controllers should never tick
  per-frame; they react to discrete typed events from the EventQueue.
- Keep functions small and focused (single responsibility). If a function
  exceeds ~50 lines, split it. Long functions are a sign of hidden
  concerns that belong in separate helpers or classes.
- Keep classes lean. Prefer composition over inheritance. Avoid sprawling
  god classes that mix rendering, logic, and I/O. If a class has more
  than 8-10 public methods, question whether it's doing too much.
- If a file or function feels "clumpsy", refactor it early. Technical debt
  compounds fast in rendering code, where debugging GPU state is already
  complex enough.
- Do not add global state unless a file already uses it and there is no
  alternative.
- When extending renderer behavior, keep GPU ownership inside Renderer or
  Data & Resource layer; avoid leaking Vulkan handles outward.
- Avoid formatting churn; keep edits minimal and localized.
- TDD: write tests first (RED), implement (GREEN), do NOT refactor beyond
  the current task's scope.
- Vulkan-HPP vk::raii: never call raw vkDestroy. Let RAII handle cleanup.
- Validation layers: test in Debug mode. Never suppress validation warnings
  without explicit justification in code comments.
- See `.github/instructions/test.instructions.md` for GPU test patterns
  (VulkanTestShared, reference-image regression, attachment conversion rules,
  scene/light scaling, Python verification).

---

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, invoke the `skill` tool with `skill: "graphify"` before doing anything else.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
