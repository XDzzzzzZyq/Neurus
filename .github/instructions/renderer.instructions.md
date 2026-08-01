# Renderer Layer

## Overview

The Renderer is a **pure rendering service** that owns GPU resources and
renders frames. It must remain stateless with respect to application logic.

## Location

- `src/render/VulkanContext.h` - Instance, physical device, logical device, queues
- `src/render/Swapchain.h` - Swapchain creation, image acquisition, presentation, recreation
- `src/render/Image.h/cpp` - GPU image with ImageState tracking (including Invalid) and mipmap generation
- `src/render/Barrier.h/cpp` - Centralized image barrier management (ImageState → Vulkan layout/stage/access)
- `src/render/RenderConfig.h` - User-settable render config: algorithms, quality params, shadow bias
- `src/render/RenderContext.h` - Per-frame immutable scene snapshot with opaque config pointer.
- `src/render/shaders/Shader.h/cpp` - Per-mesh shader container (multi-stage, owns ShaderUnits)
- `src/render/shaders/ShaderUnit.h` - Per-stage shader state: code text, parsed IR (`ShaderStruct`), SPIR-V, version
- `src/render/shaders/ShaderLibrary.h/cpp` - Load/parse/generate/compile service for shaders
- `src/render/shaders/ShaderParser.h/cpp` - GLSL → `ShaderStruct` IR (ShaderEditor struct mode)
- `src/render/shaders/ShaderGenerator.h/cpp` - `ShaderStruct` IR → GLSL
- `src/render/shaders/RenderShader.h/cpp, ComputeShader.h/cpp` - Render/compute pipeline wrappers (parsed + generated shaders)
- `src/render/shaders/ShaderCompiler.h/cpp, ShaderGPU.h` - SPIR-V compilation and GPU shader module
- `src/render/Renderer.h` - Public renderer API, frame drawing
- `src/render/RenderCache.h/cpp` - Cross-frame resource pool; owns MeshGPU, EnvironmentGPU, LightingCache, attachments, shadow maps
- `src/render/UploadManager.h/cpp` - CPU-to-GPU upload service (meshes, lights, environments, IBL)
- `src/render/Texture.h/cpp` - Texture resource (Image + sampler + descriptor)
- `src/render/resources/LightingCache.h/cpp` - GPU-side light SSBO storage (point + sun, push constants)
- `src/render/resources/MeshGPU.h` - GPU-side mesh resources (VertexBuffer + IndexBuffer) + MeshPushConstants
- `src/render/resources/EnvironmentGPU.h` - GPU-side IBL resources (diffuse + specular cubemap Textures)
- `src/render/resources/LightGPU.h` - Per-light shadow GPU resources (shadow depth cubemap/map)

## Core Responsibilities

1. **Device Management** (`VulkanContext`)
   - Create VkDevice from Qt's QVulkanInstance
   - Select physical device (prefer discrete GPU, fallback to first available)
   - Choose graphics queue family
   - Enable validation layers in Debug mode
   - Report device capabilities and extensions

2. **Swapchain Management** (`Swapchain`)
   - Create swapchain from VkSurfaceKHR (borrowed from UI layer)
   - Select surface format (prefer `VK_FORMAT_B8G8R8A8_SRGB`)
   - Select present mode (prefer `VK_PRESENT_MODE_FIFO_KHR` for VSync)
   - Clamp extent to surface capabilities
   - Create image views for each swapchain image
   - Recreate on window resize (old swapchain destroyed, new created)

3. **Shader and Pipeline Management** (`Shader`)
   - Load SPIR-V from embedded C header arrays (generated at build time)
   - Create ShaderGPU instances
   - Create vk::raii::Pipeline via VK_KHR_dynamic_rendering
   - Pipeline layout (empty for triangle; uniforms added later)

4. **Frame Rendering** (`Renderer`)
   - Command pool and command buffer creation
   - Semaphore pair: imageAvailable + renderFinished
   - Fence: inFlightFence for CPU-GPU sync
   - DrawFrame(): acquire → begin dynamic rendering → bind pipeline → draw → end → present
   - WaitIdle(): DeviceWaitIdle for clean shutdown

## Data Flow

```
UIEvents::newFrame() → Renderer::DrawFrame()
                                ├── Swapchain::AcquireNextImage()
                                ├── Begin dynamic rendering
                                ├── Bind pipeline
                                ├── Draw (3 vertices for triangle)
                                ├── End dynamic rendering
                                └── Swapchain::Present()
```

## Vulkan-HPP RAII Conventions

All Vulkan objects use the `vk::raii` namespace:

```cpp
// DO: RAII - automatically destroyed on scope exit
vk::raii::Device device(physicalDevice, deviceCreateInfo);

// DON'T: raw handles requiring manual vkDestroy
// VkDevice device;
```

**Ownership rules:**
- `vk::raii` objects are non-copyable (move-only)
- Store as class members for object lifetime
- Pass by reference when sharing (non-owning)
- Use `= delete` on copy constructor and assignment operator for classes owning GPU resources

## Validation Layers

**Debug mode:**
- Enable `VK_LAYER_KHRONOS_validation`
- Set up debug utils messenger for callback-based reporting
- Report all validation messages via UIEvents signal

**Release mode:**
- No validation layers (performance)
- Minimal error checking (asserts only for unrecoverable states)

## Error Handling

### Device Loss (`VK_ERROR_DEVICE_LOST`)
- Notify user via UIEvents
- Attempt clean shutdown (destroy resources)
- Do not crash or enter infinite loop

### Swapchain Out-of-Date (`VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`)
- Normal lifecycle event (window resize, minimize)
- Recreate swapchain with new surface dimensions
- Continue rendering with new swapchain

### Surface Lost (`VK_ERROR_SURFACE_LOST_KHR`)
- Window destroyed or display disconnected
- Clean up, stop rendering, signal application to exit

## Architectural Boundaries

### ✅ Renderer MAY:
- Read scene data via const reference
- Subscribe to UIEvents signals for configuration changes
- Own GPU resources (device, swapchain, pipeline, command buffers, RenderCache, attachments)
- Emit performance metrics or warnings via UIEvents

### ❌ Renderer MUST NOT:
- Mutate application state
- Depend on Editor or UI layers (except borrowed VkSurfaceKHR reference)
- Directly call UI or Editor functions
- Include headers from `src/editor/` or `src/ui/`

## Current Implementation (Triangle MVP)

The triangle MVP implements a minimal but correct rendering path:

1. **No vertex buffers** - Triangle vertices are hard-coded in the vertex shader
   using `gl_VertexIndex` to select positions/colors
2. **No descriptor sets** - No uniforms needed for static triangle
3. **No depth buffer** - Single triangle, no depth testing needed
4. **VK_KHR_dynamic_rendering** - No explicit VkRenderPass/VkFramebuffer objects
5. **Single command buffer** - Recorded once, replayed each frame
6. **Single in-flight frame** - No frame overlap (fence-synchronized)

## Shader Editing & Pipeline

Per-mesh shaders are editable at runtime through the Shader Editor panel. The
pipeline lives entirely in the Renderer layer (`src/render/shaders/`):

```
ShaderLibrary::LoadRenderShader(name, vertPath, fragPath)
    └── Shader (owns ShaderUnit per stage: VERTEX/FRAGMENT/COMPUTE/GEOMETRY)
          ├── ShaderUnit::code     — GLSL source text (edited in Code mode)
          ├── ShaderUnit::parsed   — ShaderStruct IR (edited in Structure mode)
          ├── ShaderUnit::spv      — compiled SPIR-V
          └── ShaderUnit::m_version— bumped on successful compile
                │
                ▼
GeometryPass pipeline (mesh shader) rebuilt when Shader::m_version changes
```

**Edit flow (event-driven):** `ShaderEditorPanel` emits `ShaderEvents`
(`ShaderCodeEdited`, `ShaderStructEdited`, `ShaderFieldAdded`) → `ShaderController`
mutates the `Mesh`'s shader data. `ShaderCreateRequested` /
`ShaderCompileRequested` recompile via `ShaderLibrary::Compile` and bump
`m_version`; the pipeline cache sees the new version and rebuilds the pipeline
on the next frame. After create/compile, `ShaderController` enqueues
`RenderResetEvent` so temporal shadow accumulation resets.

**Structure mode round-trip:** `ShaderParser` parses GLSL → `ShaderStruct` IR
(8 containers: attributes, pass outputs, inputs, outputs, uniforms, struct defs,
functions, push constants); `ShaderGenerator` regenerates GLSL from the IR.

## Current Render Pipeline

The whole pipeline is orchestrated by a **RenderGraph** (`src/render/render_graph/`).
`DeferredRenderer::m_mainGraph` is a compile-once DAG of passes; each pass declares
its image reads/writes via `Pass::GetIO()` (see `passes/Pass.h`), the graph wires
producer→consumer edges by `AttachmentName`, topologically sorts once, and
`recordFrame()` dispatches the whole pipeline with `m_mainGraph.Execute()`.

The graph is a projection of `RenderConfig`: `RebuildMainGraph()` is re-run only
when the config-derived `PipelineSignature` changes (e.g. FXAA toggled), so the
DAG always contains exactly the passes that will run. Passes still own their
barriers and descriptor writes internally; any valid topological order is correct.

Shadow resources are logical `AttachmentName` members resolved by their owning
pass (not `GetAttachment`): `ShadowDepth` (per-light bundle in `LightGPU`) and
`ShadowIntensity` (2D array via `GetShadowIntensityArray`).

```
ShadowDepthPass (per-light depth → RenderCache via GetShadowMap(uid, lightType))
    ├── Point light: cubemap geometry pass (6 faces, multiview)
    └── Sun light:   2D orthographic geometry pass (2048×2048, single view)
    │
    ▼
GeometryPass (G-Buffer MRT: Position, Normal, Albedo, MetallicRoughness, Depth)
    │
    ▼
SSAOPass (compute: reads G-Buffer, writes AO to R8 attachment)
    │
    ▼
ShadowIntensityPass (compute: per-light shadow eval → layered R8_UNORM 2D_ARRAY)
    ├── Point light: samplerCube depth comparison, PCF via cubemap sampling
    └── Sun light:   sampler2D depth comparison, ortho PCF, NDC Z in [0,1]
    │
    ▼
LightingPass (compute: reads G-Buffer + AO + shadow intensity array,
              reads LightingCache SSBOs from RenderCache, writes HDRColor)
    ├── Binding 5: PointLight SSBO (from RenderCache::GetLightingCache())
    └── Binding 6: SunLight SSBO   (from RenderCache::GetLightingCache())
    │
    ▼
IBLPass (compute: reads G-Buffer + HDRColor, applies diffuse+specular IBL, writes HDRColor)
    │
    ▼
GizmoPass (compute: reads IDBuffer, 3×3 edge detection for activeObjectId, writes R8 highlight)
    │
    ▼
ComposePass (compute: blends GizmoHighlight onto HDRColor, applies gamma correction, writes ComposedOutput)
    │
    ▼
FXAAPass (compute: reads ComposedOutput, luma-based edge detection + full-iteration edge search, writes FXAAOutput)
    │  (conditional: only when AA == AAAlg::FXAA)
    ▼
Blit (ComposedOutput or FXAAOutput) → Swapchain (vkCmdBlitImage)
```

### ImageState & Barrier Convention

All image layout transitions **MUST** go through `Barrier::Transition()`, never raw
`vk::ImageMemoryBarrier` / `vk::ImageMemoryBarrier2`:

```cpp
// DO: use Barrier
Barrier::Transition(cmdBuf, myImage, ImageState::ColorShaderRead);

// DON'T: raw Vulkan barriers on Image objects
// vk::ImageMemoryBarrier2 barrier(...); cmd.pipelineBarrier2(...);
```

- `Image` tracks its logical state via `ImageState m_state`.
- `Barrier::Transition(cmd, image, after)` reads `image.State()` as the "before"
  layout, emits a `vkCmdPipelineBarrier2`, and updates `m_state` to `after`.
- `Barrier::Transition(cmd, image, after, subresourceRange)` does the same but
  with an explicit subresource range — does **not** update `m_state` (caller
  must manage state for partial transitions).
- Raw `vk::ImageMemoryBarrier2` is acceptable **only** for:
  - Raw `VkImage` handles not wrapped in `Image` (e.g. swapchain images)
  - Same-layout memory barriers (`eGeneral → eGeneral`) within compute passes

### ImageState::Invalid Convention

- `ImageState::Invalid` signals an image whose GPU creation failed (e.g.
  missing source data, unsupported format). The image has no valid GPU resources.
- `Barrier::Transition` maps `Invalid` → `Undefined` layout (safe no-op barrier).
- `Image::FromImageData()` returns `std::shared_ptr<Image>` — on failure, returns
  a shared_ptr containing an empty Image with `ImageState::Invalid`.
- Callers should check `image.State() != ImageState::Invalid` before using the image.
- Default-constructed `Image` is empty (all handles null, `Undefined` state);
  factory functions set `Invalid` explicitly on failure.

### SSAO Convention
- **AO value**: 1.0 = fully occluded (black), 0.0 = no occlusion (lit)
- **Sampling**: Hemisphere samples in view-space, random rotation via 16×16 noise texture
- **Output**: `VK_FORMAT_R8_UNORM` attachment (`AttachmentName::SSAO`)
- **Lighting**: Ambient term multiplied by `(1.0 - ao)` so occluded areas receive less ambient light
- **Radius**: Default 0.15 (appropriate for [-1, 1] scene scale)

### GizmoPass Convention
- **IDBuffer**: Reads `VK_FORMAT_R32_UINT` attachment at binding 0, written by GeometryPass
  (stores `objectId` as a 32-bit unsigned integer per pixel).
- **Edge detection**: 3×3 Laplacian-style kernel over neighboring IDBuffer pixels.
  A pixel is highlighted when at least one neighbor has a different objectId AND
  the center pixel's objectId matches `activeObjectId` (push constant).
- **Output**: `VK_FORMAT_R8_UNORM` attachment (`AttachmentName::GizmoHighlight`) at binding 1.
  Highlighted pixels = 255 (edge of selected object); all others = 0.
- **Push constant**: `uint32_t activeObjectId` — set to the currently selected object ID
  from `RenderContext::activeObjectId`. When `activeObjectId == 0`, the pass early-outs
  (no highlight written).

### ComposePass Convention
- **Inputs**: Reads `HDRColor` (binding 0, `R16G16B16A16_SFLOAT`) and
  `GizmoHighlight` (binding 1, `R8_UNORM`).
- **Highlight blending**: When a pixel's highlight value > 0, an orange highlight
  (`rgb(1.0, 0.5, 0.0)`) is blended at 50% alpha onto the HDR color:
  `composed = mix(hdr, highlightColor, highlight * 0.5)`.
- **Gamma correction**: After highlight blending, applies `pow(color, 1.0 / gamma)` where
  `gamma` is read from `RenderConfig::r_gamma` (default 1.0) via the push constant
  `float gamma`.
- **Output**: `ComposedOutput` (`R16G16B16A16_SFLOAT`) at binding 2, the final
  framebuffer before swapchain blit.

### Sun Shadow Convention
- **Projection**: Orthographic (`glm::ortho()`) with configurable left/right/bottom/top planes and near/far planes
- **Depth range**: NDC Z in `[0, 1]` (Vulkan convention, requires `GLM_FORCE_DEPTH_ZERO_TO_ONE`)
- **Push constant**: `SunShadowPushConstants` carries `mat4 lightViewProj` (view × projection)
- **Shadow map resolution**: 2048×2048, `VK_FORMAT_D32_SFLOAT`
- **PCF**: Percentage-closer filtering via `sampler2DShadow` with UV offset kernel, ortho depth comparison
- **Shadow intensity**: Written to per-light layer in `ShadowIntensity` 2D array via `SunShadowIntensityEval` compute shader
- **Shadow bias**: Depth bias read from `RenderConfig::r_shadow_bias` (default 0.02) via `ctx.config`.
- **Shadow mode**: Supports `HARD`, `SOFT_PCF_16`, `SOFT_PCF_64` modes matching point light shadow pipeline

### Temporal Shadow Accumulation Convention

- **Jitter**: Per-frame 3D random direction (unit-ball vector from Halton(2,3,5)) applied
  as `pos_jittered = pos + light.radius * jitter` for point lights, and as UV-space
  offset `shadowUV + vec2(jitter.x, jitter.y) * uvRadius` for sun lights.
- **Accumulation**: In-place read-modify-write on the ShadowIntensity layered array.
  The compute shader reads the previous accumulated value via `imageLoad`, evaluates
  a single jittered shadow sample, and blends via EMA (exponential moving average)
  using `mix(prevAccum, sample, alpha)`, writing back via `imageStore`.
- **Alpha modes**: Two blend modes controlled by `RenderConfig::r_sampling_mode`:
  - Fixed EMA (0): `alpha = 1/8` — fast convergence
  - Moving Average (1): `alpha = 1/(iteration + 1)` — true averaging, resets on scene change
- **Iteration**: Global frame counter `m_iteration` in `DeferredRenderer`, exposed
  via `RenderContext::iteration`. Editor resets it through the event system when
  the scene changes — see `RenderResetEvent` in `events.instructions.md`.
- **Reset**: Any scene-changing event (camera move, light change, object transform,
  visibility toggle, config change, project load, asset import) enqueues
  `RenderResetEvent`. The Editor subscribes and calls
  `DeferredRenderer::ResetShadowAccumulation()` → sets `m_iteration = 0`.

### FXAA Convention
- **Algorithm**: NVIDIA FXAA 3.11, ported from cuda-vision full-iteration approach
- **Pipeline**: Two compute dispatches per frame: edge detection (Sobel gradient, subpixel offset) → edge-line iteration (march along edge, check 3 rows per step: center/upper/lower) → endpoint interpolation → bilinear resample
- **Direction**: Sobel gradient magnitude determines horizontal (`abs(gy) >= abs(gx)`) vs vertical edge
- **Offset**: Subpixel offset in `[-0.5, 0.5]` computed as `(neighborLo-center)/(neighborLo-neighborHi) - 0.5`
- **Edge march**: Iterates along the edge line (left/right for horizontal, up/down for vertical), up to 32 steps per direction. FLIP: center pixel luma differs from starting luma (crossed edge). END: off-axis neighbor has same luma as starting luma (left edge region). BOUND: out of image bounds.
- **Endpoint interpolation**: Endpoint offsets assigned by stop code (END→0, FLIP→±0.5, otherwise→original offset). Final offset = `mix(leftOff, rightOff, leftSteps/(leftSteps+rightSteps))`
- **Resample**: Bilinear `texture()` at subpixel coordinate offset by `finalOffset * rcpFrame` along the edge normal
- **Sampler**: Bilinear (`VK_FILTER_LINEAR`) for sub-pixel accuracy; falls back to nearest if `R16G16B16A16_SFLOAT` format doesn't support `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`
- **Config**: `RenderConfig::r_fxaa_subpix` (strength, default 0.75), `r_fxaa_edge_threshold` (default 0.166), `r_fxaa_edge_threshold_min` (default 0.0833)
- **Gating**: `RenderConfig::RequiresFXAA()` — only records when AA algorithm is set to FXAA in RenderConfigPanel

### RenderConfig Convention

`RenderConfig` (`src/render/RenderConfig.h`) is user-facing config owned by Editor,
passed to passes through `RenderContext::config` (opaque `void*`):

- **Algorithm selection**: `r_pipeline` (Forward/Deferred), `r_aa`, `r_ao`, `r_shadow`, `r_ssr`
- **Quality parameters**: `r_gamma`, `r_ao_ksize`, `r_ao_radius`, `r_shadow_bias` (0.02), `r_sample_pf`
- **Serialized** via cereal for project save/load
- **Live-update**: passes cast `static_cast<const RenderConfig*>(ctx.config)` each frame; scalar param changes take effect on next `DrawFrame()`
- **Shadow bias flow**: `RenderConfigPanel` slider → `configValueChanged` → `Editor::SetRenderConfig` → `RenderContext::config` → `ShadowIntensityPass` casts to `RenderConfig*`, reads `r_shadow_bias`

### Attachment Formats

All screen-space attachments (Position, Normal, Albedo, MetallicRoughness, Depth, HDRColor,
SSAO, GizmoHighlight, ComposedOutput, FXAAOutput) are created lazily via
`RenderCache::GetAttachment(name, extent)` on first use.
Per-light shadow maps are managed via `RenderCache::GetShadowMap(lightUID, lightType)`:
- `LightType::POINTLIGHT` → cubemap (6-layer 2D_ARRAY, D32_SFLOAT, 1024×1024 per face)
- `LightType::SUNLIGHT` → 2D orthographic (D32_SFLOAT, 2048×2048)
The shadow intensity array (R8_UNORM, layered 2D_ARRAY) is created via
`RenderCache::GetShadowIntensityArray(extent)` with per-light layer indices
assigned via `RenderCache::GetShadowIntensityLayer(lightUID, extent)`.

| Attachment | Format | Clear Value | Purpose |
|---|---|---|---|
| Position | R32G32B32A32_SFLOAT | (0,0,0,0) | World-space position, w=1 for rendered pixels |
| Normal | R32G32B32A32_SFLOAT | (0,0,0,0) | View-space normal |
| Albedo | R8G8B8A8_SRGB | (0,0,0,0) | Base color |
| MetallicRoughness | R8G8B8A8_UNORM | (0,0,0,0) | R=metallic, G=roughness |
| Depth | D32_SFLOAT | 1.0 | Depth buffer |
| HDRColor | R16G16B16A16_SFLOAT | (0,0,0,0) | Lighting output |
| SSAO | R8_UNORM | 0 (no occlusion) | Screen-space ambient occlusion |
| SSR | R16G16B16A16_SFLOAT | (0,0,0,0) | Screen-space reflections (planned) |
| GizmoHighlight | R8_UNORM | 0 | Selected-object edge highlight (GizmoPass output) |
| ComposedOutput | R16G16B16A16_SFLOAT | (0,0,0,0) | Final composed output before FXAA/blit (ComposePass output) |
| FXAAOutput | R16G16B16A16_SFLOAT | (0,0,0,0) | FXAA anti-aliased output (FXAAPass output, blitted when FXAA active) |
| FXAAOffsets | R16G16_SFLOAT | (0,0) | FXAA edge subpixel offsets (RG16F, 2-channel, sampled+storage) |
| ShadowMap | D32_SFLOAT | 1.0 | Per-light shadow depth (RenderCache-owned). Cubemap (6-layer 2D_ARRAY, 1024×1024) for point lights; 2D (2048×2048) for sun lights |
| ShadowIntensity | R8_UNORM | 0 (no shadow) | Layered 2D_ARRAY, one layer per shadow-casting light (RenderCache-owned) |

### RenderCache GPU Resources (MeshGPU, EnvironmentGPU)

`RenderCache` owns cross-frame mutable GPU resources beyond framebuffer attachments.
These resources separate GPU ownership from the Vulkan-free scene and asset layers:

**MeshGPU** (`src/render/MeshGPU.h`)
- Holds device-local `VertexBuffer` + `IndexBuffer` for a mesh, plus vertex/index counts
- Created lazily via `RenderCache::GetMeshGPU(objectId, meshData, device, pd, queue, qfi)`
  which uploads geometry from `MeshData` to GPU
- Subsequent calls return the cached `MeshGPU` immediately
- Destroyed via `RenderCache::RemoveMeshGPU(objectId)` or `RenderCache::Clean()`
- Scene `Mesh` objects call `RenderCache::GetMeshGPU()` through `Mesh::UploadToGPU()`;
  the scene layer never owns GPU buffers directly

**EnvironmentGPU** (`src/render/resources/EnvironmentGPU.h`)
- Holds diffuse irradiance and specular prefiltered cubemap `Texture` objects
  (each wraps `Image` + sampler + descriptor)
- Created lazily via `RenderCache::CreateEnvironmentGPU(envId, device, pd, queue, qfi, env)`
  from an `Environment` scene object
- Read per-frame by `LightingPass` via `RenderCache::GetEnvironmentGPU(envId)`
- Destroyed via `RenderCache::RemoveEnvironmentGPU(envId)`

**LightingCache** (`src/render/resources/LightingCache.h`)
- Manages point light and sun light SSBOs (device-local GPUBuffers)
- Created by `RenderCache` via `InitLightingCache(queue, qfi)` (separated from constructor
  so queue/qfi don't need to be stored as members)
- Updated via `RenderCache::UpdateLighting(variantDict)` — accepts a map of
  `variant<PointLightStruct, SunLightStruct>` keyed by light UID
- `RenderCache::GetLightingCache()` returns the LightingCache for per-frame SSBO binding
  by `LightingPass`
- Also defines `PointLightStruct`, `SunLightStruct` (std140-compatible, 48 bytes),
  and `LightingPushConstants` (176 bytes) — byte-for-byte matches with GLSL shaders

**MeshPushConstants** (`src/render/resources/MeshGPU.h`)
- Per-mesh push-constant block sent to the vertex shader (128 bytes total)
- Two mat4s: `model` (local-to-world transform) and `normalMatrix`

**GeometryRenderItem** (removed)
- Previously mixed CPU (MeshData) and GPU (VertexBuffer, IndexBuffer) concerns in one struct
- Removed as part of CPU/GPU isolation refactoring. GPU resources now live in `MeshGPU`;
  CPU data stays in `MeshData`

## RenderGraph

The deferred pipeline is orchestrated by a **RenderGraph** (`src/render/render_graph/`),
built on the generic `neurus::Graph<SData, NData>` DAG template (`src/core/Graph.h`).

**Model**
- `RenderGraph` = `Graph<AttachmentName, PassEntry>`. One node per pass; a node's
  `PassEntry` holds the `Pass*` and its cached `PassIO`.
- Each pass declares its image I/O via `Pass::GetIO()` returning a `PassIO`
  (name + read/write `AttachmentBinding`s: resource, binding slot, descriptor
  type, image layout). This is the single declaration the graph consumes.
- Edges are keyed by `AttachmentName`: `Connect(producer, resource, consumer)`
  links the producer's output socket for that resource to the consumer's input
  socket. Fan-out (one write → many reads) is supported.

**Lifecycle**
- `AddPass(pass)` — materializes one socket per declared read/write from `GetIO()`.
- `Connect(...)` — wires producer→consumer; self-loops and duplicate edges are
  rejected (returns `false`).
- `Compile()` — Kahn topological sort. Inputs with no in-graph producer are
  **external** (produced by a RenderCache attachment) and are allowed. A cycle
  throws `std::runtime_error` naming the involved passes.
- `Execute(cmd, cache, ctx)` — invokes each pass's `Record()` in compiled order.
- `Clear()` — drops all nodes so the graph can be rebuilt.

**Config-driven topology (single source of truth = RenderConfig)**
- `DeferredRenderer::m_mainGraph` holds the whole pipeline. It is rebuilt
  (`RebuildMainGraph`) only when the `PipelineSignature` derived from
  `RenderConfig` changes (currently FXAA on/off), so the DAG always contains
  exactly the passes that will run — no per-node "enabled" state to drift.

**Resources & descriptors**
- `DescriptorBinder` (`render_graph/DescriptorBinder.h`) translates a pass's
  `PassIO` image bindings into descriptor-set writes.
- Shadow resources are logical `AttachmentName` members resolved by their owning
  pass, **not** `GetAttachment`: `ShadowDepth` (per-light bundle in `LightGPU`)
  and `ShadowIntensity` (2D array via `GetShadowIntensityArray`). `ConfigFor`
  throws for them to catch accidental single-attachment fetches.

**Ownership boundary (current)**
- Passes still own their own barriers and descriptor writes inside `Record()`.
  The graph only orders and dispatches. Moving barrier/descriptor/resource
  ownership into the graph (and enabling transient aliasing + custom passes) is
  tracked in issue #32; parallel execution built on it in #33–#37.

**Tests**: `test/render/test_render_graph.cpp` — socket materialization,
connect/duplicate rejection, external inputs, linear/fan-out/diamond ordering,
multi-resource edges, cycle detection (with named nodes), rebuild determinism,
clear/reuse, execute preconditions.

## Future Evolution

- IBL enhancements (BRDF LUT, multi-scattering)
- Shadow mapping improvements (multi-light, shadow evaluation pass)
- SSR (Screen-Space Reflections): ray marching variants
- Tonemapping: filmic (ACES) + gamma correction
- FXAA: luma-based edge anti-aliasing
- VMA integration for memory management
- Multiple in-flight frames (double/triple buffering) — done (`kMaxFramesInFlight`)
- Threaded command buffer recording — see issues #35 / #37
- Pipeline cache for faster startup
- Render graph abstraction — done (see the RenderGraph section above); graph-owned
  resources/barriers/descriptors tracked in #32
