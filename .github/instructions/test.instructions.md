# Testing Standards & Roles

## Overview

Neurus uses **Google Test** for both GPU-dependent and non-GPU unit tests.
GPU tests require a Vulkan 1.4-capable device and are excluded from CI;
non-GPU tests (editor layer, events) run on every CI push.

## Test Organization

```
test/
├── CMakeLists.txt              # Test build configuration
├── shared/                     # Shared test infrastructure
│   ├── test_main.cpp               # Google Test main() entry point
│   ├── TestVulkanShared.h          # GPU test fixture base class + static helpers
│   ├── TestVulkanShared.cpp        # Bootstrap: Instance → Device → Queue → CommandPool
│   ├── TestReferenceImage.h        # Reference-image comparison utilities
│   ├── TestCornellBox.h            # Cornell box scene builder
│   └── TestSimpleShadow.h          # Shadow test scene builder
├── editor/                     # Non-GPU tests (run in CI)
│   ├── test_event_bus.cpp          # Qt UIEvents singleton tests
│   ├── test_event_bus_typed.cpp    # Typed EventQueue tests (+ expanded event tests)
│   ├── test_context.cpp            # EditorContext tests (consolidated)
│   ├── test_input.cpp              # Input state tests
│   ├── test_camera_controller.cpp  # Event-driven MMB camera controls
│   ├── test_selection.cpp          # Selection manager tests
│   └── test_scene_status.cpp       # Scene status tracking tests
├── asset/                      # Asset layer tests
│   ├── test_imagedata.cpp          # ImageData loading and decoding
│   └── test_meshdata.cpp           # MeshData OBJ parsing
├── scene/                      # Scene layer tests
│   ├── test_camera.cpp             # Camera transforms and projection
│   ├── test_debug.cpp              # Debug visualization helpers
│   ├── test_light.cpp              # Light data structures
│   ├── test_mesh.cpp               # Mesh data and GPU upload
│   ├── test_scene.cpp              # Scene graph (consolidated)
│   ├── test_scene_integration.cpp  # Scene + renderer integration
│   ├── test_sprite.cpp             # Sprite/overlay tests
│   ├── test_transform.cpp          # Transform hierarchy tests
│   └── test_uid.cpp                # Unique ID generation tests
├── project/                    # Project serialization tests
│   ├── test_default_project.cpp    # Default project creation
│   └── test_project_roundtrip.cpp  # Project save/load round-trip
├── render/                     # GPU tests (excluded from CI)
│   ├── test_attachments.cpp
│   ├── test_buffers.cpp
│   ├── test_commandbuffer.cpp
│   ├── test_compute_pipeline.cpp
│   ├── test_deferred_shading.cpp   # Reference-image regression test
│   ├── test_descriptor.cpp
│   ├── test_gbuffer.cpp
│   ├── test_ibl.cpp
│   ├── test_ibl_render.cpp
│   ├── test_image.cpp
│   ├── test_lighting.cpp
│   ├── test_material.cpp
│   ├── test_mesh.cpp
│   ├── test_model_render.cpp
│   ├── test_pipeline.cpp
│   ├── test_renderpass.cpp
│   ├── test_scene_wiring.cpp       # Overrides SetUp for swapchain
│   ├── test_screenshot.cpp
│   ├── test_shader_module.cpp
│   ├── test_shadow_cubemap.cpp
│   ├── test_ssao.cpp
│   ├── test_syncobjects.cpp
│   ├── test_texture.cpp
│   ├── test_vulkan_buffer.cpp
│   ├── test_vulkan_context.cpp
│   ├── reference/                  # Reference images for regression tests
│   │   ├── deferred/               # Deferred-pass reference PNGs
│   │   │   ├── Position.png
│   │   │   ├── Normal.png
│   │   │   ├── Albedo.png
│   │   │   ├── MetallicRoughness.png
│   │   │   └── HDRColor.png
│   │   ├── ibl/                    # IBL reference images
│   │   │   ├── ibl_render.png
│   │   │   └── test_gradient.hdr
│   │   ├── shadow/                 # Shadow cubemap reference images
│   │   │   ├── CubemapDepth_Face0.png
│   │   │   ├── CubemapDepth_Face1.png
│   │   │   ├── CubemapDepth_Face2.png
│   │   │   ├── CubemapDepth_Face3.png
│   │   │   ├── CubemapDepth_Face4.png
│   │   │   └── CubemapDepth_Face5.png
│   │   └── ssao/                   # SSAO reference image
│   │       └── SSAO.png
│   └── render/                     # (CMake-source-directory for render tests)
```

## Test Fixture Hierarchy

### VulkanTestShared (base class for ALL GPU tests)

Location: `test/shared/TestVulkanShared.h`

Responsibilities:
- Create `vk::raii::Instance` (with surface extensions + debug-utils in Debug)
- Enumerate physical devices, prefer discrete GPU
- Find graphics queue family
- Create `vk::raii::Device` + `vk::Queue`
- Create `vk::raii::CommandPool` + one-shot command buffers
- Provide helpers: `BeginCmd()`, `EndSubmitWait(cmd)`, `ResolveAssetPath()`

```cpp
class VulkanTestShared : public ::testing::Test
{
protected:
    bool HasVulkan() const;
    vk::raii::PhysicalDevice& PhysicalDevice();
    vk::raii::CommandBuffer& BeginCmd();       // Returns a begun one-shot cmd buffer
    void EndSubmitWait(vk::raii::CommandBuffer& cmd);  // Submits + waitIdle
    static std::string ResolveAssetPath(const char* assetRelative);

    bool m_hasVulkan = false;
    std::unique_ptr<vk::raii::Device> m_device;
    vk::Queue m_queue = nullptr;
    uint32_t m_graphicsQueueFamily = 0;
    // ... (full state in header)
};
```

Inheriting tests MUST:
- Call `VulkanTestShared::SetUp()` first in their own `SetUp()` override
- Check `if (!m_hasVulkan) return;` after the base `SetUp()` call
- Call `VulkanTestShared::TearDown()` in their `TearDown()` override
- Use `PhysicalDevice()` for format/limit queries
- Use `BeginCmd()` / `EndSubmitWait(cmd)` for all GPU command sequences

### Override SetUp for Special Requirements

Some tests need extensions the base class doesn't enable. Override `SetUp`
completely and re-bootstrap with your extensions:

```cpp
void SetUp() override
{
    if (!HasVulkan()) return;  // May be true from parent
    // ... custom Instance/Device creation with needed extensions ...
}
```

See `test_scene_wiring.cpp` for an example that enables `VK_KHR_swapchain`.

### Shared Static Helpers

In addition to the protected member methods above, `VulkanTestShared` provides
8 static helper functions/structs for common test operations. These eliminate
duplicated code across test files:

| Helper | Signature | What it does |
|--------|-----------|-------------|
| `TestVertex` struct | `posX/Y/Z, nrmX/Y/Z, uvX/uvY` | 32-byte vertex matching `GeometryPass::BufferLayout` (3 pos + 3 normal + 2 UV floats) |
| `HalfToFloat(uint16_t)` | `static float` | IEEE 754 half-precision (16-bit) to single-precision (32-bit) float conversion |
| `FindMemoryType(pd, typeBits, required)` | `static uint32_t` | Memory type index lookup matching type bits and property flags for staging/buffer allocation |
| `TransitionGbufferToColorAttachment(am, fixture)` | `static void` | Transitions all 4 color attachments (Position/Normal/Albedo/MetallicRoughness) to `eColorAttachmentOptimal` and Depth to `eDepthStencilAttachmentOptimal` |
| `ComputeCameraUBO(Camera&)` | `static CameraUBOData` | Computes view and view-projection matrices from a `Camera` object |
| `MakeTestCamera(w, h)` | `static CameraUBOData` | Creates a default 60° FOV camera at `(0,0,2)` looking at origin with given aspect ratio |
| `TestTriangle()` | `static pair<vector<TestVertex>, vector<uint32_t>>` | Returns a 3-vertex triangle (XY plane, facing +Z) + 3 indices, suitable for quick geometry tests |
| `ReadbackHdrOutput(device, pd, queue, qfi, am, w, h)` | `static vector<float>` | Reads back HDRColor RGBA16F attachment to CPU; handles layout transition, staging copy, and half→float conversion |

Usage example — replace 15+ lines of manual G-Buffer transitions with one call:

```cpp
// Instead of manually transitioning each attachment:
VulkanTestShared::TransitionGbufferToColorAttachment(*m_attachmentManager, *this);
```

All helpers are defined in `test/shared/TestVulkanShared.h`. Tests that need a
helper not yet available should add it to the shared header rather than
duplicating it in the local test file.

## Writing a GPU Test

### Pattern (established in this session)

1. **Include** the shared fixture header and renderer headers
2. **Inherit** from `VulkanTestShared`
3. **Construct** renderer components (AttachmentManager, passes, etc.) in `SetUp()`
4. **Test body**:
   a. Load assets (OBJ meshes via `ResolveAssetPath()`)
   b. Create scene objects (Camera, Light, Mesh)
   c. Upload vertex/index buffers with staging → device-local
   d. Record and submit geometry pass
   e. Upload per-frame data (light SSBO, camera UBO)
   f. Record and submit lighting/compute pass
   g. Capture or verify output

```cpp
#include <gtest/gtest.h>
#include "shared/TestVulkanShared.h"
#include "render/AttachmentManager.h"
#include "render/GeometryPass.h"
// ... other includes ...

class MyFeatureTest : public VulkanTestShared { /* ... */ };

TEST_F(MyFeatureTest, DoesWhatItShould)
{
    if (!m_hasVulkan) GTEST_SKIP() << "No Vulkan GPU.";
    // ... test logic ...
}
```

## Coordinate Space Conventions for Test Scenes

| Quantity | Space | Format | Notes |
|----------|-------|--------|-------|
| Position attachment | World-space | `R16G16B16A16_SFLOAT` | w=1.0 for rendered pixels, w=0.0 for clear |
| Normal attachment | View-space | `R16G16B16A16_SFLOAT` | w=0.0. Remap `(val+1)*0.5` when saving to PNG |
| Camera position | World-space | `vec3` | Push constant to compute shader |
| Light positions | World-space | `vec3` | In PointLightGpu SSBO |
| View matrix | Camera→World | `mat4` | `glm::lookAt()`, stored in UBO + push constant |
| Projection | Clip-space | `mat4` | `glm::perspective()` (may need depth-range fix) |

### Critical: Object Scale and Light Placement

This was the single most impactful debugging lesson from the deferred shading test:

**Object scale matters.** An OBJ mesh at unit scale (radius ≈ 1) seen from
distance 5 at 60° FOV fills roughly 36% of the viewport. If smaller coverage
is desired, scale vertex positions:
```cpp
v.posX = s[0] * 0.25f;  // Scale to 25%
```

**Light placement matters.** Inverse-square attenuation (`1/d²`) is aggressive:
- Light at distance 2 → attenuation = 1/4 = 25%
- Light at distance 3 → attenuation = 1/9 = 11%
- Light at distance 5 → attenuation = 1/25 = 4%

For visible lighting with typical power values (10-100), keep lights within
2-4 units of the geometry surface. A light at `(3,3,3)` with the sphere at
origin gives distance ≈ 5.2; moving it to `(2,2,2)` gives distance ≈ 3.5.

**Camera position** relative to geometry also changes what portion of the
scene is visible. A camera at `(0,2,5)` looking at origin sees the sphere
from above-front; the visible hemisphere has negative world-space z.

## Reference-Image Regression Testing

### Pattern (first-run → second-run comparison)

The deferred shading test (`test_deferred_shading.cpp`) establishes this
pattern for GPU output validation:

1. **First run**: Test enumerates 5 attachments (Position, Normal, Albedo,
   MetallicRoughness, HDRColor). For each, check if `reference/deferred/Name.png`
   exists. If ANY is missing, capture all 5 as PNGs into the reference directory
   and `GTEST_SKIP()`.
2. **Second run**: All reference images exist. Capture attachments as `.tmp`
   PNGs, load both captured and reference, compare pixel-by-pixel with a
   per-channel tolerance of ±2. FAIL on any mismatch.

### Attachment-to-PNG Conversion Rules

Each attachment format maps to RGBA8 PNG differently:

| Attachment | Format | remapSigned | Conversion |
|------------|--------|-------------|------------|
| Position   | `R16G16B16A16_SFLOAT` | `false` | half→float→clamp[0,1]→U8 |
| Normal     | `R16G16B16A16_SFLOAT` | `true`  | half→float→`(val+1)*0.5`→clamp→U8 |
| Albedo     | `R8G8B8A8_SRGB` | `false` | raw U8 (sRGB-encoded) |
| MetallicRoughness | `R8G8B8A8_Unorm` | `false` | raw U8 |
| HDRColor   | `R16G16B16A16_SFLOAT` | `false` | half→float→clamp[0,1]→U8 |

**Background pixels** (no geometry rendered) have:
- Position: `(0,0,0,0)` → PNG black
- Normal: `(0,0,0,0)` → PNG (0,0,0) after signed remap → (127,127,127) after
  `(val+1)*0.5` for zero → actually background check in ConvertHalfToU8 writes
  alpha=0 for background normal pixels
- Albedo: `(0,0,0,0)` → PNG black
- MetallicRoughness: `(0,0,0,0)` → PNG black

### Generating and Verifying Reference Images

```bash
# Delete old references
rm test/render/reference/deferred/*.png

# Run test — generates + skips
cd build/debug && ctest -R DeferredShading

# Run test again — compares against newly generated references
cd build/debug && ctest -R DeferredShading
```

**Always verify references with Python** before committing:

```python
from PIL import Image
from collections import Counter

img = Image.open("test/render/reference/deferred/HDRColor.png")
pixels = list(img.getdata())
unique = Counter(pixels)
print(f"Unique values: {len(unique)} out of {len(pixels)}")

# Check: if only 1 unique value, lighting may be broken (should vary across sphere)
```

Key things to check:
- **Position**: should have a visible blob with varying R/G values (B may be 0
  depending on camera orientation). Non-black pixel count = rendered sphere area.
- **Normal**: should have smoothly varying RGB values on sphere pixels. Uniform
  normals suggest a transform bug.
- **Albedo**: white on sphere pixels (255), black (0) on background. If ALL
  pixels are white, the clear isn't working or geometry covers everything.
- **MetallicRoughness**: R=0 (metallic), G=127-128 (roughness 0.5) on sphere,
  black on background. If background shows non-zero, clear isn't working.
- **HDRColor**: should show visible lighting (brighter on the lit side). If
  uniformly dark (~8) everywhere, lighting contribution is zero.

## Running Tests

```bash
# Build tests
cmake --build build/debug

# Run all tests
cd build/debug && ctest --output-on-failure

# Run a specific test by name pattern
cd build/debug && ctest -R DeferredShading

# Run a specific test case
cd build/debug && ctest -R "DeferredShadingTest.GbufferAttachments"

# Run only non-GPU tests (CI-compatible)
cd build/debug && ctest -E "DeferredShading|Lighting|Screenshot|ModelRender|Texture"

# Run the test executable directly with GTest filters
build/debug/Debug/neurus_test --gtest_filter="DeferredShadingTest.*"
```

### Filtering Test Output

Coupled test output can be hard to parse. Use these patterns to pinpoint failures:

```bash
# PowerShell: Get only passed/failed summary
cd build/debug; ctest --output-on-failure 2>&1 | Select-String -Pattern "tests passed|tests failed"

# PowerShell: Get ALL failing test names (***Failed marker)
cd build/debug; ctest --output-on-failure 2>&1 | Select-String -Pattern "\*\*\*Failed"

# PowerShell: Filter to a specific subsystem
cd build/debug; ctest -R "SceneWiring|SSAO|Lighting" --output-on-failure 2>&1 | Select-String -Pattern "FAIL|pass|fail"

# PowerShell: Run test binary directly for cleaner output
./build/debug/Debug/neurus_test.exe --gtest_filter="SceneWiring*" 2>&1 | Select-String -Pattern "FAILED|Running|OK|PASSED"

# PowerShell: List failing tests only (gtest summary line)
./build/debug/Debug/neurus_test.exe --gtest_filter="SceneWiring*" 2>&1 | Select-String -Pattern "FAILED.*test"

# Bash/Git Bash: Same patterns but use grep instead of Select-String
cd build/debug && ctest --output-on-failure 2>&1 | grep -E "tests (passed|failed)|FAILED"
```

### Test Working Directory

CTest runs the test exe with `WorkingDirectory = build/debug/test/`. The
`ResolveAssetPath()` helper tries multiple relative paths (`../../../res/obj/`)
to find asset files.

## Debugging GPU Test Failures

### HDRColor is Black / Near-Black

Checklist (in order):

1. **Position attachment early-out**: `posSample.w < 0.5` in the compute shader.
   If Position.w is 0 (clear value), all pixels early-out → pure black output.
   Check the Position PNG: if it shows a sphere, w should be 1.0 on those pixels.

2. **Light distance**: With inverse-square falloff, a light 5+ units away from
   the surface may contribute only 4% or less of the light power. Move the light
   closer or increase power.

3. **Object scale**: A coarse icosphere (20 triangles, radius ≈ 1) can fill most
   of a 256×256 viewport at 60° FOV from distance 5. Scale vertices down with
   `pos * 0.25f`.

4. **Descriptor bindings**: Verify that the compute shader's bindings (0-3 for
   G-Buffer samplers, 4 for storage image, 5 for light SSBO) match the
   `DescriptorSetLayout` bindings in `LightingPass::CreateDescriptorSetLayout()`.

5. **SSBO data**: Check that the `PointLightGpu` struct in C++ matches the
   `PointLight` struct in GLSL exactly (same offsets, same padding, same total
   size of 48 bytes). Use `static_assert` in C++.

6. **Push constants**: Verify the `LightingPushConstants` struct alignment
   matches the GLSL push constant layout (96 bytes total: int + pad12 +
   vec4 + mat4).

7. **Validation layers**: Run in Debug mode. If no validation errors print,
   the API usage is correct and the issue is algorithmic.

### G-Buffer Attachments All Uniform

If Albedo, MetallicRoughness, and HDRColor show the same values on ALL pixels
(both sphere and background), suspect:

1. **Dynamic rendering clear not working**: Verify `vk::AttachmentLoadOp::eClear`
   is being specified and the clear values are correct.
2. **Geometry covers all pixels**: Very coarse meshes (20-triangle icosphere)
   can have large projected triangles. Scale down or move camera back.
3. **Pipeline color blend writes everywhere**: Check no attachment has
   `blendEnable = VK_TRUE` with source factor that bleeds across pixels.

### Validation Error Debug

If validation layers print an error:

1. Read the error message to identify the violated VUID.
2. Check the VUID in the Vulkan spec (or search web for the exact error).
3. Fix the root cause — never suppress validation errors with `#ifndef _DEBUG`.

### Runtime Validation Error Capture

Vulkan validation errors are routed through a `vk::DebugUtilsMessengerEXT`
callback function, NOT directly to stdout or stderr. This means standard
output redirection (`2>&1`) may or may not capture validation messages
depending on how the callback is implemented.

**Common validation error patterns**:

1. **Wrong image view type**: Creating `VkImageView` with
   `VK_IMAGE_VIEW_TYPE_CUBE` for per-mip sub-resources of a cubemap is
   invalid — each per-mip view covers one array layer (not 6). Use
   `VK_IMAGE_VIEW_TYPE_2D_ARRAY` with `layerCount = 6` instead.

2. **Missing command pool reset flag**: `vk::CommandPoolCreateInfo` without
   `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT` will trigger
   `VK_ERROR_VALIDATION_FAILED_EXT` if `vkResetCommandBuffer()` is called
   explicitly.

**Why validation errors can be invisible**:

| Output channel | Visible in terminal? | Captured by `2>&1`? |
|---|---|---|
| `std::cout` from callback | Yes | Yes (stdout) |
| `std::cerr` from callback | Yes | Yes (stderr) |
| File-only logger | No | No |
| Silent/no-op callback | No | No |
| `OutputDebugString` (VS Debug Output window) | No | No |

**How to capture validation errors reliably**:

1. **Use `2>&1` always** — this merges stdout and stderr, capturing both
   `std::cout` and `std::cerr` from any debug callback:
   ```
   $output = & "build/debug/Debug/Neurus.exe" 2>&1
   ```
   Then grep for `VUID-` to detect any validation violations.

2. **Verify the debug callback implementation** in `VulkanContext.cpp` or
   `TestVulkanShared.cpp`. The callback should write to `std::cerr`, not
   `std::cout`, so validation messages survive stderr-only captures:
   ```cpp
   static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
       VkDebugUtilsMessageSeverityFlagBitsEXT severity,
       VkDebugUtilsMessageTypeFlagsEXT /*type*/,
       const VkDebugUtilsMessengerCallbackDataEXT* data,
       void* /*userData*/) {
       std::cerr << "[VALIDATION] " << data->pMessage << std::endl;
       return VK_FALSE;
   }
   ```

3. **Explicitly verify after fixing** suspected validation issues by running
   the application and checking for remaining VUIDs:
   ```
   $output = & "build/debug/Debug/Neurus.exe" 2>&1
   if ($output -match "VUID-") { Write-Host "VALIDATION ERROR STILL PRESENT" }
   ```

4. **Be aware of the IDE pitfall**: Visual Studio's Debug Output window
   (`OutputDebugString`) can receive validation messages through a separate
   channel. The application may appear clean in the IDE while silently
   violating validation rules when run outside it. Always verify from a
   clean terminal, not from within the IDE.

## CI Expectations

- **Non-GPU tests** (editor layer, events): Run on every CI push. Must pass.
- **GPU tests**: Excluded from CI (no Vulkan 1.4 device in GitHub Actions).
  Must pass locally before merging.
- **Reference images**: Committed to the repository. If a PR changes rendered
  output, the reference images must be regenerated and re-committed.

### Regenerating Reference Images for a PR

1. Delete the affected `test/render/reference/*/*.png` files.
2. Run the test twice (first run generates, second run compares).
3. Verify the new references with Python (`PIL.Image` analysis).
4. Commit the new PNGs alongside the code changes.
5. The PR reviewer should verify the visual change is expected.

## GPU Test Patterns (Quick Reference)

These patterns were established during deferred PBR development and apply to all GPU tests:

- **Inherit `VulkanTestShared`** (`test/shared/TestVulkanShared.h`). The base class bootstraps Instance → PhysicalDevice → Device → Queue → CommandPool.
- **Use `BeginCmd()` and `EndSubmitWait(cmd)`** for every GPU command sequence. `BeginCmd()` returns a one-shot command buffer; `EndSubmitWait(cmd)` submits and waits-idle.
- **Reference-image tests**: "first-run-generates, second-run-compares" pattern. First run captures attachments as PNGs to `reference/deferred/` and SKIPs; second run compares pixel-by-pixel against those references with ±2 tolerance.
- **Attachment format conversions**: HDRColor (`R16G16B16A16_SFLOAT`) converts via half→float→clamp→U8. Normal attachments use signed remap `(val+1)*0.5` before clamp.
- **Test scene scale**: A sphere OBJ (radius ≈ 1) fills ~36% of a 256×256 viewport at 60° FOV from distance 5. Scale vertices with `pos * 0.25f` or reposition camera/light as needed.
- **Light placement**: Inverse-square attenuation `1/d²` is aggressive. Keep test lights within 2-4 units of geometry for visible lighting. A light at distance 5 produces only ~4% radiance vs distance 2.
- **Debugging black HDRColor**: First check that Position.w > 0 (the early-out in the compute shader), then check attenuation distance, light power, and descriptor bindings. Don't assume "zero lighting" means the compute shader is broken.
- **Verify references with Python** before committing: use `PIL.Image` to check uniform values, unique pixel counts, and histogram analysis.
- **Use `Mesh::UploadToGPU()` instead of raw VBO/IBO** in rendering tests. Instead of creating `VertexBuffer`/`IndexBuffer` directly (which duplicates GPU upload logic and bypasses the Mesh abstraction), load geometry through `MeshData` and `Mesh`:
  ```cpp
  // BEFORE (direct buffer creation — only for buffer-specific tests):
  VertexBuffer vbo(device, pd, queue, qfi, vertexData, size, stride, count);
  IndexBuffer  ibo(device, pd, queue, qfi, indexData, isize, icount);

  // AFTER (required for rendering/feature tests):
  auto meshData = std::make_shared<MeshData>();
  meshData->LoadObjFromString(kObjString);  // or LoadObj(path)
  auto mesh = std::make_shared<Mesh>();
  mesh->o_mesh = meshData;
  mesh->UploadToGPU(device, pd, queue, qfi);
  // Access GPU resources via:
  //   mesh->GetVertexBuffer(), mesh->GetIndexBuffer(), mesh->GetGPUIndexCount()
  ```
  **Exemption**: `test_buffers.cpp` and `test_vulkan_buffer.cpp` are the unit
  tests for `VertexBuffer`/`IndexBuffer`/`VulkanBuffer` — they use raw buffers
  by design and are exempt from this rule.
- **"Extract, Don't Duplicate"**: Before writing a helper function in a test
  file, check if `VulkanTestShared` already provides it (see Shared Static
  Helpers above). If a helper is needed across 2+ test files, extract it to
  `TestVulkanShared.h` as a static method. Do **not** duplicate vertex structs,
  half-float converters, G-Buffer transition logic, or camera UBO computation
  — they are already centralized.

## Common Pitfalls Summary

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Object too large | Geometry fills entire viewport, G-Buffer uniform | `pos * 0.25f` |
| Light too far | HDRColor = ambient only (8,8,8) | Move light to dist 2-4 |
| Light radius too small | Attenuation too strong | Set `light_radius = 10.0f` |
| Push constants misaligned | Shader reads garbage | Check struct alignment |
| SSBO struct mismatch | Light data corrupted | `static_assert(sizeof == 48)` |
| Missing depth-range define | Wrong clip-space Z range | Add `GLM_FORCE_DEPTH_ZERO_TO_ONE` |
| Wrong normal space | Lighting doesn't match geometry | VS stores view-space, compute transforms back |
| First-run reference stale | Test passed against old reference | Delete PNGs, regenerate, verify |
| No `VK_EXT_DEBUG_UTILS` | Deferred validation errors | Include in Debug builds |
| Manual Vulkan bootstrap | Test file has its own Instance/Device/Queue/CommandPool setup (~70 lines) | Inherit `VulkanTestShared` base class |
| Raw VBO/IBO in rendering test | Test creates `VertexBuffer`/`IndexBuffer` directly | Use `Mesh::UploadToGPU()` |
| Duplicated `TestVertex` struct | `struct TestVertex { ... }` defined locally in test file | Use `TestVertex` from `TestVulkanShared.h` |
| Duplicated helper functions | `HalfToFloat`, `TransitionGbufferToColorAttachment`, `FindMemoryType` repeated across files | Use static helpers from `VulkanTestShared` |
| Placeholder/stub test file | File with single `GTEST_SKIP` test | Delete from CMakeLists.txt; do NOT write new test logic unless planned |

## Complete Development Cycle Checklist

Every subagent, feature implementation, or bugfix MUST complete ALL of
the following steps before declaring the task done.  Partial completion
is NOT acceptable -- skip none of these steps.

1. **Build → 0 errors**
   ```bash
   cmake --build build/debug
   ```
   No compilation errors, no linker errors.  Warnings should be addressed
   or justified.

2. **Run `Neurus.exe` → check terminal output**
   ```powershell
   $output = & "build/debug/Debug/Neurus.exe" 2>&1; Start-Sleep -Seconds 3; Write-Host $output
   ```
   Scan for:
   - `VUID-` validation violations
   - `[VALIDATION]` prefixed errors from the debug messenger
   - Crashes (access violations, segfaults)
   - Unexpected `NEURUS_ERR` log lines

3. **Run tests → all pass**
   ```bash
   cd build/debug && ctest --output-on-failure
   ```
   ALL tests must pass.  Do not ignore failures; fix them or update
   reference images if the change is intentional.

4. **Verify feature works visually**
   - Screenshots: check that `TakeScreenshotAllAttachments()` exports
     all relevant attachments (screenshots/ directory).
   - Visual check: the rendered viewport shows the expected result
     (lighting, shadows, geometry, etc.).
   - Runtime behaviour: resize the window, interact with the viewport,
     verify no deadlocks or freezes.

5. **No stubs, TODOs, or placeholders remain**
   - No `// TODO`, `// FIXME`, `// STUB`, or placeholder comments
     left behind in the changed files.
   - No hardcoded magic numbers without documentation.
   - No commented-out code blocks unless explicitly justified.
