# Build & Test

## Prerequisites

- Visual Studio 2022 (MSVC C++20 toolchain)
- CMake >= 3.27
- Vulkan SDK 1.4.x (with `$env:VULKAN_SDK` set)
- Qt 6.8+ (with `CMAKE_PREFIX_PATH` pointing to install)
- GNU Make (via MSYS2, Chocolatey, or Git Bash)

## Quick Start

```
# Clone with submodules
git clone --recurse-submodules https://github.com/XDzzzzzZyq/Neurus.git
cd Neurus

# Init submodules, download pre-compiled deps, configure CMake
make update

# Build debug
cmake --build build --config Debug

# Build release
cmake --preset win && cmake --build build --config Release

# Generate VS 2022 solution (outside source tree)
make nobuild
# Opens: ../Neurus_VS2022/Neurus.sln

# Build and run tests
make check
```

## Dependency System

Neurus supports two modes for third-party dependencies that can be compiled ahead of time:

### Pre-compiled (recommended)

Run `make update` to download pre-compiled binaries from the
[Neurus-Lib](https://github.com/XDzzzzzZyq/Neurus-Lib) GitHub Release.
Binaries are extracted into `lib/<platform>/` (git-ignored).

The CMake build system automatically detects pre-compiled libraries and skips
source builds for:

- **shaderc** — shader compilation library (saves ~40-60s per build)
- **qtadvanceddocking** — Qt Advanced Docking System (saves ~15s per build)

### Source build (fallback)

If `lib/<platform>/` does not contain pre-compiled binaries, CMake falls
back to building from source in `dep/` (the existing behavior). This
requires a one-time shaderc dependency sync:

```
cd dep/shaderc
python utils/git-sync-deps
```

## CI

- See `.github/workflows/ci.yml` for the exact matrix and steps.
- CI runs Windows x64 and macOS arm64. GPU tests are excluded from CI.
- CI attempts to fetch pre-compiled dependencies first; falls back to source build on failure.

## Testing

- Framework: Google Test
- Non-GPU tests run in CI (UIEvents, EventQueue, EditorContext)
- GPU tests require a Vulkan 1.4-capable device
- Run all tests: `make check` (or `cd build && ctest -C Debug --output-on-failure`)
- Run specific tests: `make check FILTER="-R DeferredShading"`
- Build only the test binary: `make build test` (or `make test`)
- On local machine, launch `Neurus.exe` to check terminal output and runtime errors.
- See `.github/instructions/test.instructions.md` for full testing standards and patterns.

## Lint / Format

- No repo-wide formatter configured.
- Follow Blender C/C++ style guidelines (see `.github/instructions/style.instructions.md`).
- Do not run clang-format on project code unless explicitly requested.
