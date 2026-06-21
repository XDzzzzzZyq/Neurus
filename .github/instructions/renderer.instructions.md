# Renderer Layer

## Overview

The Renderer is a **pure rendering service** that owns GPU resources and
renders frames. It must remain stateless with respect to application logic.

## Location

- `src/render/VulkanContext.h` - Instance, physical device, logical device, queues
- `src/render/Swapchain.h` - Swapchain creation, image acquisition, presentation, recreation
- `src/render/ShaderProgram.h` - SPIR-V loading, pipeline creation
- `src/render/Renderer.h` - Public renderer API, frame drawing

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

3. **Shader and Pipeline Management** (`ShaderProgram`)
   - Load SPIR-V from embedded C header arrays (generated at build time)
   - Create vk::raii::ShaderModule instances
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
- Own GPU resources (device, swapchain, pipeline, command buffers)
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

## Current Render Pipeline

```
GeometryPass (G-Buffer MRT: Position, Normal, Albedo, MetallicRoughness, Depth)
    │
    ▼
SSAOPass (compute: reads G-Buffer, writes AO to R8 attachment)
    │
    ▼
LightingPass (compute: reads G-Buffer + AO, writes HDRColor)
    │
    ▼
Blit HDRColor → Swapchain (vkCmdBlitImage)
```

### SSAO Convention
- **AO value**: 1.0 = fully occluded (black), 0.0 = no occlusion (lit)
- **Sampling**: Hemisphere samples in view-space, random rotation via 16×16 noise texture
- **Output**: `VK_FORMAT_R8_UNORM` attachment (`AttachmentName::SSAO`)
- **Lighting**: Ambient term multiplied by `(1.0 - ao)` so occluded areas receive less ambient light
- **Radius**: Default 0.15 (appropriate for [-1, 1] scene scale)

### Attachment Formats
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

## Future Evolution

- IBL (Image-Based Lighting): diffuse + specular cubemaps, BRDF LUT
- Shadow mapping: per-light depth maps, shadow evaluation
- SSR (Screen-Space Reflections): ray marching variants
- Tonemapping: filmic (ACES) + gamma correction
- FXAA: luma-based edge anti-aliasing
- VMA integration for memory management
- Multiple in-flight frames (double/triple buffering)
- Threaded command buffer recording
- Pipeline cache for faster startup
- Render graph abstraction
