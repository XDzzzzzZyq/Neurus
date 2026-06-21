# Neurus

**C++20 Vulkan-HPP 1.4 Real-time Renderer**

A real-time rendering framework with strict four-layer architecture
(Renderer ↔ Editor ↔ UI ↔ Data & Resource). Built for experimentation
with modern rendering algorithms.

[![CI](https://github.com/XDzzzzzZyq/Neurus/actions/workflows/ci.yml/badge.svg)](https://github.com/XDzzzzzZyq/Neurus/actions/workflows/ci.yml)

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│ UI Layer (Qt6 QML)                                          │
│  owns: VkSurfaceKHR, QWindow, UIEvents (QObject singleton)   │
└─────────────────────┬────────────────────────────────────────┘
                      │ Qt Signals/Slots
                      ▼
┌──────────────────────────────────────────────────────────────┐
│ Editor Layer                                                │
│  owns: Controllers, SelectionManager, EditorContext         │
└─────────────────────┬────────────────────────────────────────┘
                      │
            ┌─────────┴──────────┐
            ▼                    ▼
┌──────────────────┐  ┌────────────────────────────────────────┐
│ Data & Resource   │  │ Renderer Layer (Vulkan-HPP vk::raii)   │
│  owns: allocators │  │  owns: VkInstance, VkDevice, VkQueue,  │
│  descriptor pools │  │   VkSwapchainKHR, VkPipeline,          │
│  pipeline cache   │  │   VkCommandBuffer, all GPU resources   │
└──────────────────┘  └────────────────────────────────────────┘
```

## Prerequisites

- **Visual Studio 2022** (MSVC C++20 toolchain)
- **CMake** >= 3.27
- **Vulkan SDK** 1.4.x ([LunarG Vulkan SDK](https://vulkan.lunarg.com/))
  - Set `VULKAN_SDK` environment variable
- **Qt 6.8+** ([Qt for Windows](https://www.qt.io/download))
  - Set `CMAKE_PREFIX_PATH` to Qt install (e.g., `C:\Qt\6.8.0\msvc2022_64`)
- **GNU Make** (via [MSYS2](https://www.msys2.org/), [Chocolatey](https://chocolatey.org/), or Git Bash)

## Quick Start

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/XDzzzzzZyq/Neurus.git
cd Neurus

# Configure and build (Debug)
cmake --preset default
cmake --build build/debug

# Run the application
./build/debug/Neurus.exe

# Build Release
cmake --preset release
cmake --build build/release

# Generate Visual Studio 2022 solution
make nobuild
# Opens: ../Neurus_VS2022/Neurus.sln

# Run tests
make test
# Or manually:
cd build/debug && ctest --output-on-failure
```

## Project Structure

```
Neurus/
├── .github/
│   ├── instructions/       # Architecture/component docs
│   └── workflows/          # CI configuration
├── cmake/                  # CMake helper modules
├── dep/                    # Git submodule dependencies
├── res/shaders/            # GLSL shader source files
├── src/
│   ├── render/             # Renderer layer (Vulkan-HPP)
│   ├── editor/             # Editor layer (logic, controllers)
│   │   └── controllers/    # Controller implementations (CameraController)
│   ├── ui/                 # UI layer (Qt6 Widgets + ADS)
│   │   └── qml/            # QML source files
│   ├── data/               # Data & Resource layer
│   └── main.cpp            # Application entry point
├── test/
│   ├── render/             # Renderer unit tests
│   └── editor/             # Editor unit tests
├── AGENTS.md               # AI agent instructions
├── CMakeLists.txt          # Root CMake build
├── CMakePresets.json       # CMake presets
├── Makefile                # Convenience build wrapper
└── README.md               # This file
```

## Current Scope (Deferred PBR MVP)

The current deliverable is a deferred PBR renderer with geometry pass,
lighting compute pass, and full G-Buffer pipeline:

- Vulkan-HPP RAII instance, device, swapchain, pipeline
- `VK_KHR_dynamic_rendering` for render passes
- Qt6 Widgets window with Qt-Advanced-Docking-System (ADS)
- Viewport as dockable central widget via ADS `CenterDockWidgetArea`
- Qt Signals/Slots UIEvents singleton (UI↔Editor)
- Typed EventQueue for Editor↔Renderer event dispatch
- Swapchain recreation on window resize
- Validation layers in Debug builds
- Embedded SPIR-V shaders (compiled at CMake time)
- Non-GPU Google Test samples (UIEvents, EventQueue, EditorContext)
- Event-driven CameraController (orbit/zoom/dolly/pan via EventQueue)
- OBJ mesh loading with MeshData (icosphere, cube, etc.)
- Deferred PBR pipeline: GeometryPass (G-Buffer) + LightingPass (compute)
- Reference-image regression tests (capture → compare PNG)

## Code Style

Follows [Blender C/C++ guidelines](https://developer.blender.org/docs/handbook/guidelines/c_cpp/):

- **Indentation**: Tabs
- **Braces**: Allman style
- **Naming**: PascalCase for types and methods
- **Comments**: Doxygen-style `/** @brief ... */` on public APIs
- **RAII**: All resources initialized in constructor, released in destructor
- **Includes**: Local first, then third-party, then STL; use `#pragma once`

## License

[MIT](LICENSE)
