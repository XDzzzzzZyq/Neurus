# Code Style Guidelines

## Language / Standard

- C++20 (see `CMakeLists.txt`)
- Vulkan-HPP `vk::raii` namespace for automatic RAII resource management

## Formatting (Blender C/C++ Guidelines)

- Indentation: tabs
- Braces: Allman style (opening brace on new line)
- Keep lines <= 120 characters where practical

## Includes

- Local project headers before system headers
- Group: local, third-party (Vulkan, Qt, GLM), STL
- Use `#pragma once` in headers

## Naming

- Types / classes: PascalCase (e.g., `VulkanContext`, `ShaderProgram`)
- Methods: PascalCase (e.g., `DrawFrame()`, `AcquireNextImage()`)
- Members: prefixes as needed (`m_` for member variables, or match local style)
- Functions in .cpp: camelCase if local; match surrounding convention
- Enums: PascalCase for types, PascalCase or UPPER_CASE for values
- Files: PascalCase for class files (e.g., `VulkanContext.h`)

## RAII (Non-Negotiable)

- All resource-owning classes initialize fully in constructor, release in
  destructor. No separate `Init()`/`Terminate()` methods.
- Classes owning GPU/OS resources: `= delete` copy constructor and assignment.
- Use `std::unique_ptr`/`std::shared_ptr` for explicit ownership.
- Raw pointers for non-owning references only.

## Const Correctness

- Use `const` for non-mutating methods and references.
- Renderer receives `const` scene/context data.

## Error Handling (Karpathy Guidelines)

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

## Debug Printing (Karpathy Guidelines)

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

## Comments / Documentation

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
- Use a normal hyphen (`-`) instead of an em dash (`–` or `—`).

## Shader Conventions

- GLSL source: `res/shaders/`
- SPIR-V compiled at CMake configure time via glslangValidator
- Embedded as C headers in `${CMAKE_BINARY_DIR}/generated/shaders/`
