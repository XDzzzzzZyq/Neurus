# AGENTS.md
# Guidance for agentic coding in this repo

This repository is a C++20 Vulkan-HPP 1.4 real-time renderer with strict layer
isolation (Renderer ↔ Editor ↔ UI ↔ Data & Resource). Use this file as the
single source of truth for commands, architectural rules, and code style
expectations.

--------------------------------------------------------------------------------
CURRENT SCOPE (Triangle MVP)
--------------------------------------------------------------------------------

The project is in its INITIALIZATION phase. The current deliverable is a
working colored RGB triangle rendered via the full four-layer architecture.

**In scope:**
- Vulkan-HPP RAII instance, device, swapchain, pipeline (vk::raii path kept as reference)
- QVulkanWindow-based triangle rendering (primary rendering path for MVP)
- VK_KHR_dynamic_rendering (vk::raii path) + traditional render pass (QVulkanWindow path)
- Qt6 Widgets window with Qt-Advanced-Docking-System (ADS)
- Viewport as dockable central widget via ADS `CenterDockWidgetArea`
- Docks: Shader Editor (left), Viewport (center), Outliner + Properties + Render Config (right), Texture Viewer (bottom)
- Qt Signals/Slots UIEvents singleton (UI↔Editor)
- Typed EventBus (EventPool) for Editor↔Renderer event dispatch
- Swapchain recreation on window resize (both paths)
- Validation layers in Debug builds
- Embedded SPIR-V shaders (compiled at CMake time)
- Non-GPU Google Test samples (UIEvents, EventBus, EditorContext)
- Dock layout persistence (save/restore via ADS serialization)

**Out of scope (post-MVP):**
- glTF/OBJ/PNG file loading, asset pipeline
- PBR materials, multi-pass rendering, deferred shading
- Compute shaders, ray tracing, mesh shaders
- Undo/redo, serialization, plugin system
- Linux/macOS support
- Threading, VMA, profilers, shader hot-reload

--------------------------------------------------------------------------------
BUILD / LINT / TEST
--------------------------------------------------------------------------------

**Prerequisites:**
- Visual Studio 2022 (MSVC C++20 toolchain)
- CMake >= 3.27
- Vulkan SDK 1.4.x (with $env:VULKAN_SDK set)
- Qt 6.8+ (with `CMAKE_PREFIX_PATH` pointing to install)
- GNU Make (via MSYS2, Chocolatey, or Git Bash)

**Build commands:**
```
# Configure + build debug
cmake --preset default
cmake --build build/debug

# Build release
cmake --preset release
cmake --build build/release

# Generate VS 2022 solution (outside source tree)
make nobuild
# Opens: ../Neurus_VS2022/Neurus.sln

# Build and run tests
make test
```

**CI reference:**
- See `.github/workflows/ci.yml` for the exact matrix and steps.
- CI runs Windows x64 only. GPU tests are excluded from CI.

**Tests:**
- Framework: Google Test
- Non-GPU tests run in CI (UIEvents, EventBus, EditorContext)
- GPU tests require a Vulkan 1.4-capable device
- Run all tests: `cd build/debug && ctest --output-on-failure`
- Run a single test: `cd build/debug && ctest -R UIEvents_Singleton`
- On local machine, launch `Neurus.exe` to check the terminal output and any runtime error.

**Lint / format:**
- No repo-wide formatter configured.
- Follow Blender C/C++ style guidelines (see Style section below).
- Do not run clang-format on project code unless explicitly requested.

--------------------------------------------------------------------------------
ARCHITECTURE RULES (HARD REQUIREMENTS)
--------------------------------------------------------------------------------

**Four-layer architecture:**

```
┌──────────────────────────────────────────────────────────────┐
│ UI Layer (Qt6 QML)                                          │
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

**Layer boundaries:**
- **Renderer**: pure rendering service; owns GPU resources; consumes read-only
  scene data; must not mutate application-level state.
- **Editor**: application logic and scene mutation; owns Controllers;
  communicates via Context, UIEvents (Qt signals), and EventBus (typed events).
- **UI**: Qt QML presentation only; owns surface; emits signals.
- **Data & Resource**: GPU resource management; descriptor pools, allocators,
  buffer/image abstractions. (Stub for MVP.)

**Communication:**
- Cross-layer communication MUST go through:
  - UIEvents (Qt Signals/Slots) for UI↔Editor signals
  - EventBus (typed EventPool) for Editor↔Renderer events
  - Context objects for data queries
- Direct coupling across layers is forbidden.
- Renderer must not include Editor or UI headers.
- UI must not include Renderer headers (only surface handle).

**Vulkan ownership:**
- UI Layer: owns `VkSurfaceKHR` (created from QWindow + QVulkanInstance)
- Renderer Layer: owns everything else (instance via QVulkanInstance sharing,
  device, swapchain, pipeline, command pool, all GPU resources)
- No shared ownership of GPU resources across layers.
- Non-copyable GPU resource classes (`= delete` copy/assign).

**Threading:**
- Main thread only for MVP.
- Future: Renderer may run on dedicated thread with proper synchronization.

--------------------------------------------------------------------------------
CODE STYLE GUIDELINES
--------------------------------------------------------------------------------

**Language / standard:**
- C++20 (see CMakeLists.txt)
- Vulkan-HPP `vk::raii` namespace for automatic RAII resource management

**Formatting (Blender C/C++ guidelines):**
- Indentation: tabs
- Braces: Allman style (opening brace on new line)
- Keep lines <= 120 characters where practical

**Includes:**
- Local project headers before system headers
- Group: local, third-party (Vulkan, Qt, GLM), STL
- Use `#pragma once` in headers

**Naming:**
- Types / classes: PascalCase (e.g., `VulkanContext`, `ShaderProgram`)
- Methods: PascalCase (e.g., `DrawFrame()`, `AcquireNextImage()`)
- Members: prefixes as needed (`m_` for member variables, or match local style)
- Functions in .cpp: camelCase if local; match surrounding convention
- Enums: PascalCase for types, PascalCase or UPPER_CASE for values
- Files: PascalCase for class files (e.g., `VulkanContext.h`)

**RAII (non-negotiable):**
- All resource-owning classes initialize fully in constructor, release in
  destructor. No separate `Init()`/`Terminate()` methods.
- Classes owning GPU/OS resources: `= delete` copy constructor and assignment.
- Use `std::unique_ptr`/`std::shared_ptr` for explicit ownership.
- Raw pointers for non-owning references only.

**Const correctness:**
- Use `const` for non-mutating methods and references.
- Renderer receives `const` scene/context data.

**Error handling (Karpathy Guidelines):**
- Follow the four Karpathy principles below. No deviation.
- **Never use try-catch to hide or swallow errors.** Every catch block MUST
  either re-throw (with enhanced message) or print the error.
  Silent `catch (...) {}` is forbidden.
- Use `catch (const std::exception& e)` and `NEURUS_ERR(e.what())` when
  recovery is legitimate (e.g., swapchain out-of-date, acquire failure).
- `assert()` for precondition violations (debug-only). Do not use try-catch for
  logic flow.
- Handle `VK_ERROR_DEVICE_LOST` and `VK_ERROR_OUT_OF_DATE_KHR` as normal
  lifecycle events - print the error, then recover.
- Early return on invalid state after printing the reason.

**Debug printing (Karpathy Guidelines):**
- Use the `NEURUS_LOG` / `NEURUS_ERR` macros from `core/Log.h` - they
  automatically inject `[func:line]` prefix for traceability:
  ```cpp
  NEURUS_LOG("[Swapchain] " << extent.width << "x" << extent.height);
  NEURUS_ERR("Texture::createFromPixelData failed: " << e.what());
  ```
- `NEURUS_LOG` - debug-only info output (`std::cout`). Compiles to nothing in
  Release. Use for constructor diagnostics, key parameters, lifecycle events.
- `NEURUS_ERR` - always-on error output (`std::cerr`). Active in all builds.
  Use in catch blocks and unrecoverable error paths.
- Always print debug information in constructors of large classes (Swapchain,
  DeferredRenderer, etc.) - dimension, image count, format, key parameters.

**Comments / documentation:**
- Doxygen-style on all public APIs:
  ```cpp
  /**
   * @brief Short one-line description.
   * @param ctx Read-only rendering context.
   * @return true if frame was drawn successfully.
   * @note Caller must ensure the surface is valid.
   */
  ```
- Inline comments follow Blender C/C++ style: `// comment` (space after //)
- Non-trivial sections get `// --- Section Name ---` separators

**Shader conventions:**
- GLSL source: `res/shaders/`
- SPIR-V compiled at CMake configure time via glslangValidator
- Embedded as C headers in `${CMAKE_BINARY_DIR}/generated/shaders/`

**Version Control:**
- Prefer Git submodule.
- For the renaming and moving, use `git mv` to track the history.
- Complete and Double check all aspects (tests, coding style, document) of the current task before commit.
 
--------------------------------------------------------------------------------
FILE LAYOUT
--------------------------------------------------------------------------------

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
│   ├── render/             # Renderer layer (Vulkan-HPP + QVulkanWindow)
│   │   ├── QVulkanRenderer.h/cpp  # QVulkanWindowRenderer (primary path)
│   │   ├── Renderer.h/cpp         # vk::raii path (reference)
│   │   ├── ShaderProgram.h/cpp
│   │   ├── Swapchain.h/cpp
│   │   └── VulkanContext.h/cpp
│   ├── editor/             # Editor layer (logic, controllers)
│   │   ├── events/          # Event system (UIEvents + typed EventBus)
│   │   │   ├── UIEvents.h/cpp    # Qt signal bus for UI↔Editor
│   │   │   ├── EventBus.h        # Typed EventPool dispatcher (no Qt)
│   │   │   └── EditorEvents.h    # Event type structs
│   │   ├── EditorContext.h/cpp
│   │   └── CMakeLists.txt
│   ├── ui/                 # UI layer (Qt6 Widgets + ADS)
│   │   ├── NeurusMainWindow.h/cpp # Main window with ADS dock manager
│   │   ├── VulkanWindow.h/cpp     # QVulkanWindow subclass
│   │   ├── MainWindow.h/cpp       # (legacy QWindow subclass)
│   │   ├── VulkanWidget.h/cpp     # (legacy vk::raii widget)
│   │   └── qml/            # QML source files (legacy)
│   ├── data/               # Data & Resource layer
│   └── main.cpp            # Application entry point
├── test/
│   ├── render/             # Renderer unit tests
│   └── editor/             # Editor unit tests
├── AGENTS.md               # This file
├── CMakeLists.txt           # Root CMake build
├── CMakePresets.json        # CMake presets (default, release, vs2022)
├── Makefile                 # Convenience build wrapper
└── README.md               # Project introduction
```

--------------------------------------------------------------------------------
PRACTICAL GUIDANCE FOR AGENTS
--------------------------------------------------------------------------------

- Respect layer isolation; do not introduce cross-layer header includes where
  forbidden.
- Prefer UIEvents/EventBus/Context-driven flows over direct calls across layers.
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

--------------------------------------------------------------------------------
END
--------------------------------------------------------------------------------
