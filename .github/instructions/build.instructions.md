# Build & Test

## Prerequisites

- Visual Studio 2022 (MSVC C++20 toolchain)
- CMake >= 3.27
- Vulkan SDK 1.4.x (with `$env:VULKAN_SDK` set)
- Qt 6.8+ (with `CMAKE_PREFIX_PATH` pointing to install)
- GNU Make (via MSYS2, Chocolatey, or Git Bash)

## Quick Start

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

## CI

- See `.github/workflows/ci.yml` for the exact matrix and steps.
- CI runs Windows x64 only. GPU tests are excluded from CI.

## Testing

- Framework: Google Test
- Non-GPU tests run in CI (UIEvents, EventQueue, EditorContext)
- GPU tests require a Vulkan 1.4-capable device
- Run all tests: `cd build/debug && ctest --output-on-failure`
- Run a single test: `cd build/debug && ctest -R DeferredShading`
- On local machine, launch `Neurus.exe` to check terminal output and runtime errors.
- See `.github/instructions/test.instructions.md` for full testing standards and patterns.

## Lint / Format

- No repo-wide formatter configured.
- Follow Blender C/C++ style guidelines (see `.github/instructions/style.instructions.md`).
- Do not run clang-format on project code unless explicitly requested.
