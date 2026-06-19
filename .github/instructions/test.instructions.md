# Testing Standards & Roles

## Overview

Neurus uses **Google Test** for both GPU-dependent and non-GPU unit tests.
GPU tests require a Vulkan 1.4-capable device and are excluded from CI;
non-GPU tests (editor layer, events) run on every CI push.

## Test Organization

```
test/
├── CMakeLists.txt          # Test build configuration
├── shared/                 # Shared test infrastructure
│   ├── TestVulkanShared.h      # GPU test fixture base class
│   ├── TestVulkanShared.cpp    # Bootstrap: Instance → Device → Queue → CommandPool
│   └── test_main.cpp           # Google Test main() entry point
├── editor/                 # Non-GPU tests (run in CI)
│   ├── test_uievents.cpp
│   ├── test_eventbus.cpp
│   └── test_editor_context.cpp
├── render/                 # GPU tests (excluded from CI)
│   ├── test_attachments.cpp
│   ├── test_gbuffer.cpp
│   ├── test_deferred_shading.cpp    # Reference-image regression test
│   ├── test_lighting.cpp
│   ├── test_model_render.cpp
│   ├── test_screenshot.cpp
│   ├── test_gpu_resource_cache.cpp
│   ├── test_scene_wiring.cpp        # Overrides SetUp for swapchain
│   ├── test_texture.cpp
│   └── reference/                  # Reference images for regression tests
│       └── deferred/               # Deferred-pass reference PNGs
│           ├── Position.png
│           ├── Normal.png
│           ├── Albedo.png
│           ├── MetallicRoughness.png
│           └── HDRColor.png
└── render/                          # (CMake-source-directory for render tests)
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

For visible lighting with typical power values (10–100), keep lights within
2–4 units of the geometry surface. A light at `(3,3,3)` with the sphere at
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
- **MetallicRoughness**: R=0 (metallic), G=127–128 (roughness 0.5) on sphere,
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

4. **Descriptor bindings**: Verify that the compute shader's bindings (0–3 for
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

## Common Pitfalls Summary

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Object too large | Geometry fills entire viewport, G-Buffer uniform | `pos * 0.25f` |
| Light too far | HDRColor = ambient only (8,8,8) | Move light to dist 2–4 |
| Light radius too small | Attenuation too strong | Set `light_radius = 10.0f` |
| Push constants misaligned | Shader reads garbage | Check struct alignment |
| SSBO struct mismatch | Light data corrupted | `static_assert(sizeof == 48)` |
| Missing depth-range define | Wrong clip-space Z range | Add `GLM_FORCE_DEPTH_ZERO_TO_ONE` |
| Wrong normal space | Lighting doesn't match geometry | VS stores view-space, compute transforms back |
| First-run reference stale | Test passed against old reference | Delete PNGs, regenerate, verify |
| No `VK_EXT_DEBUG_UTILS` | Deferred validation errors | Include in Debug builds |
