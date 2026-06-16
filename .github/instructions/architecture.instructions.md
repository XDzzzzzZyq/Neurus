# Architecture Overview

## System Design Philosophy

This is a C++20 Vulkan-HPP 1.4 real-time renderer designed for experimentation
with modern rendering algorithms. The architecture prioritizes:

- **Strict layer isolation** - Renderer ↔ Editor ↔ UI ↔ Data & Resource
  boundaries must not be violated
- **Minimal global state** - State is explicit and localized
- **Explicit data flow** - Communication via UIEvents (Qt Signals/Slots),
  EventBus (typed EventPool), and Context objects
- **Stateless rendering** - Renderer does not own application-level state
- **Deterministic GPU resource management** - Vulkan resources have explicit
  RAII ownership via `vk::raii` namespace

## Four-Layer Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                   UI Layer (Qt6 QML)                         │
│  (Window, surface, UIEvents, user input)                    │
└──────────────────────┬───────────────────────────────────────┘
                       │ Qt Signals/Slots (UIEvents)
                       │ + Typed Events (EventBus)              │
                       ▼
┌──────────────────────────────────────────────────────────────┐
│                Editor Layer                                  │
│  (Application logic, controllers, scene state, event system)│
└──────────────────────┬───────────────────────────────────────┘
                       │
             ┌─────────┴──────────┐
             ▼                    ▼
┌────────────────────┐  ┌──────────────────────────────────────┐
│ Data & Resource     │  │       Renderer Layer                 │
│ (GPU allocators,    │  │ (Pure rendering service, GPU        │
│  descriptor pools,  │  │  resources, swapchain, pipeline)    │
│  buffer/image abs)  │  │                                     │
└────────────────────┘  └──────────────────────────────────────┘
```

### Layer Responsibilities

**Renderer Layer** (`src/render/`)
- Pure rendering service with no application logic
- Owns GPU resources (device, swapchain, pipeline, command buffers)
- Owns VkDevice, VkSwapchainKHR, VkPipeline, VkCommandPool
- Consumes read-only VkSurfaceKHR from UI layer
- Must NOT mutate application state
- Must NOT depend on Editor or UI layers

**Editor Layer** (`src/editor/`)
- Contains application logic and scene mutation
- Owns Controllers (Camera, Selection, etc.)
- Manages EditorContext (scene + editor state)
- Owns UIEvents (Qt signals) and EventBus (typed EventPool)
- Communicates with Renderer via Context and typed EventBus
- Must NOT directly manipulate GPU resources

**UI Layer** (`src/ui/`)
- Qt6 QML-based presentation layer
- Owns VkSurfaceKHR (created from QWindow + QVulkanInstance)
- Owns EventBus singleton exposure to QML
- Displays data and captures user input
- Emits Qt signals for state changes
- Must NOT directly access Renderer internals
- Must NOT mutate scene state directly

**Data & Resource Layer** (`src/data/`)
- GPU resource management abstractions (Buffer, Image, DescriptorSet)
- Memory allocation strategies (stub for MVP)
- Pipeline cache management (future)
- Asset pipeline integration (future - file loading, serialization)

### Communication Protocols

**UIEvents System** (Qt Signals)
- QObject singleton with typed Qt signals
- UI↔Editor layer dispatch
- Main signals: `renderRequested()`, `windowResized(int, int)`
- Decoupled: emitters don't know subscribers

**EventBus System** (Typed EventPool)
- Header-only template-based event dispatcher (no Qt dependency)
- Editor↔Renderer event dispatch with deferred Process()
- Subscribe: `EventBus().subscribe<T>(handler)`
- Enqueue: `EventBus().enqueue(event)`
- Dispatch: `EventBus().Process()` (call once per frame)

**Context System** (Data)
- `EditorContext` - Scene + editor state
- Read-only access to scene data for Renderer
- Data flows: Editor mutates, Renderer consumes

### Vulkan Ownership Graph (Critical)

```
UI Layer owns:
  QWindow → VkSurfaceKHR

Renderer Layer owns:
  VkInstance (shared via QVulkanInstance)
  VkDevice + VkQueue
  VkSwapchainKHR (consumes UI's VkSurfaceKHR)
  VkPipeline + VkPipelineLayout
  VkCommandPool + VkCommandBuffers
  All framebuffer attachments (via dynamic rendering)

Data & Resource Layer owns (future):
  VkBuffer + VkDeviceMemory pairs
  VkImage + VkDeviceMemory + VkImageView triples
  VkDescriptorPool + VkDescriptorSet
```

## Design Constraints

### Architectural Invariants

1. **No cross-layer direct coupling** - Use UIEvents/EventBus/Context only
2. **Renderer is stateless** - Application state lives in Editor
3. **Full RAII** - No two-phase initialization; no `Init()`/`Terminate()` methods
4. **Explicit GPU ownership** - Each Vulkan handle has one owning layer
5. **Non-copyable GPU resources** - `= delete` copy/assign
6. **Vulkan validation** - Debug builds enable `VK_LAYER_KHRONOS_validation`

### Naming Conventions

- Classes: PascalCase (e.g., `VulkanContext`, `ShaderProgram`)
- Functions: PascalCase for public API; camelCase for internals
- Members: `m_` prefix for member variables
- Constants: UPPER_SNAKE_CASE
- Files: PascalCase for headers/sources

### Formatting / Includes

- Indentation: tabs
- Braces: Allman style
- Includes: local headers before system; group local, third-party (Vulkan, Qt,
  GLM), STL; use `#pragma once`

### Ownership / Error Handling

- All resource-owning classes follow RAII
- Classes owning GPU or OS resources delete copy constructor and assignment
- Use `std::unique_ptr`/`std::shared_ptr` for explicit ownership
- Raw pointers for non-owning references only
- Debug builds: Vulkan validation layers enabled
- Handle `VK_ERROR_DEVICE_LOST` and `VK_ERROR_OUT_OF_DATE_KHR` gracefully

## Current Scope (Triangle MVP)

The project delivers a working colored RGB triangle through the full
architecture. This proves the stack end-to-end before adding features.

**Features in scope:**
- Vulkan-HPP RAII instance/device/swapchain/pipeline
- VK_KHR_dynamic_rendering for single-pass triangle
- Qt6 QML window (800×600, resizable)
- EventBus with renderRequested signal
- Validation layers (Debug only)
- Swapchain recreation on resize
- Non-GPU unit tests (UIEvents, EventBus, EditorContext)

**Features out of scope:**
- PBR, multi-pass, deferred rendering
- Compute shaders, ray tracing
- Asset loading (glTF, OBJ, PNG)
- Undo/redo, serialization
- VMA, threading, profilers
- Linux/macOS support

## Future Architecture Evolution

- Multi-pass deferred rendering pipeline
- PBR material system
- GPU-driven rendering with indirect draws
- Threaded command buffer recording
- Vulkan Memory Allocator (VMA) integration
- Asset pipeline (glTF loading, texture compression)
- Render graph for automatic barrier/resource management
- Ray tracing extensions
