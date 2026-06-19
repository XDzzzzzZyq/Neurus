# Architecture Overview

## System Design Philosophy

This is a C++20 Vulkan-HPP 1.4 real-time renderer designed for experimentation
with modern rendering algorithms. The architecture prioritizes:

- **Strict layer isolation** - Renderer ↔ Editor ↔ UI ↔ Asset
  boundaries must not be violated
- **Minimal global state** - State is explicit and localized
- **Explicit data flow** - Communication via UIEvents (Qt Signals/Slots),
  EventBus (typed EventPool), and Context objects
- **Stateless rendering** - Renderer does not own application-level state
- **Deterministic GPU resource management** - Vulkan resources have explicit
  RAII ownership via `vk::raii` namespace

## Three-Layer Architecture + Supporting Modules

```
┌──────────────────────────────────────────────────────────────┐
│                   UI Layer (Qt6)                              │
│  (Window, surface, UIEvents, user input)                    │
└──────────────────────┬───────────────────────────────────────┘
                       │ Qt Signals/Slots (UIEvents)
                       │ + Typed Events (EventBus)
                       ▼
┌──────────────────────────────────────────────────────────────┐
│                Editor Layer                                  │
│  (Application logic, controllers, scene state, event system)│
└──────────────────────┬───────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────────┐
│               Renderer Layer (Vulkan-HPP vk::raii)           │
│  (All GPU resources: device, swapchain, pipeline,            │
│   buffers, images, descriptors, compute & geometry passes)  │
├──────────────────────────────────────────────────────────────┤
│               Asset Layer (src/asset/)                       │
│  (OBJ mesh loading, PNG/HDR decoding, CPU-side data)        │
└──────────────────────────────────────────────────────────────┘

Scene objects (src/scene/) and Core utilities (src/core/, src/project/)
are shared across layers.
```

### Layer Responsibilities

**Renderer Layer** (`src/render/`)
- Pure rendering service with no application logic
- Owns ALL GPU resources (device, swapchain, pipeline, command buffers, buffers, images, descriptors)
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
- Qt6 Widgets presentation layer (MainWindow, dock panels via Qt-Advanced-Docking-System)
- Owns VkSurfaceKHR (created from QWindow + QVulkanInstance, or via VulkanWidget)
- Hosts QVulkanWindow for GPU rendering viewport
- Displays data and captures user input
- Emits Qt signals for state changes
- Must NOT directly access Renderer internals
- Must NOT mutate scene state directly

**Asset Layer** (`src/asset/`)
- OBJ mesh loading and parsing into MeshData
- PNG/HDR image decoding into ImageData
- CPU-side data representation; GPU upload handled by Renderer
- Must NOT issue draw calls or manage GPU pipelines

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

  Also through src/render/ abstractions:
  VkBuffer + VkDeviceMemory pairs (VulkanBuffer)
  VkImage + VkDeviceMemory + VkImageView triples (VulkanImage, Texture)
  VkDescriptorPool + VkDescriptorSet (DescriptorManager)
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

## Current Scope

The project delivers a deferred PBR renderer with geometry pass, lighting
compute pass, and full G-Buffer pipeline through the four-layer architecture.

**Features in scope:**
- Vulkan-HPP RAII instance, device, swapchain, pipeline
- VK_KHR_dynamic_rendering for render passes
- Qt6 Widgets window with Qt-Advanced-Docking-System (ADS)
- Viewport as dockable central widget
- Qt Signals/Slots UIEvents singleton (UI↔Editor)
- Typed EventBus (EventPool) for Editor↔Renderer event dispatch
- Swapchain recreation on window resize
- Validation layers in Debug builds
- Embedded SPIR-V shaders (compiled at CMake time)
- OBJ mesh loading with MeshData
- Deferred PBR pipeline: GeometryPass (G-Buffer) + LightingPass (compute)
- Screenshot capture + TextureData PNG readback
- GPU tests with shared VulkanTestShared base class
- Reference-image regression tests (capture → compare PNG)
- Render caches: GpuResourceCache, DescriptorCache
- RenderPassManager for dynamic rendering pass control

**Features out of scope:**
- glTF/PNG file loading, asset pipeline (OBJ loading is in scope)
- Multi-pass rendering (SSAO, SSR), IBL, shadows
- Ray tracing, mesh shaders
- Undo/redo, serialization, plugin system
- Linux/macOS support
- Threading, VMA, profilers, shader hot-reload

## Future Architecture Evolution

- Multi-pass deferred rendering pipeline
- PBR material system
- GPU-driven rendering with indirect draws
- Threaded command buffer recording
- Vulkan Memory Allocator (VMA) integration
- Asset pipeline (glTF loading, texture compression)
- Render graph for automatic barrier/resource management
- Ray tracing extensions
