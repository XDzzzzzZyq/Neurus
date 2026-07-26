# Asset Layer & GPU Resource Management

## Overview

The Asset layer (`src/asset/`) manages **loading, storing, and providing access**
to external asset data (meshes, images). It is **Vulkan-free**: no `<vulkan/>`
includes appear in public headers. Format queries use the `PixelFormat` enum
rather than raw `vk::Format` values. GPU resource abstractions (buffers,
images, descriptors) are owned by the **Renderer layer** (`src/render/`) -- see
`renderer.instructions.md` for the Buffer class hierarchy (Buffer, StagingBuffer, GPUBuffer, UniformBuffer), Image, Texture, and
DescriptorManager.

The original "Data & Resource" layer concept has been absorbed: asset loading
lives in `src/asset/`, GPU resource management lives in `src/render/`.

## Location

- `src/asset/MeshData.h/cpp` - Mesh geometry data (vertices, indices)
- `src/asset/ImageData.h/cpp` - Image pixel data (CPU-side, owning vector, PNG/HDR save)
- `src/asset/PixelFormat.h` - Vulkan-free pixel format enum for CPU-side format queries (PixelByteSize, ChannelCount, IsSRGB, IsHDR helpers)
- `src/asset/Project.h/cpp` - Pure registration-based serializer (no data ownership)
- `src/asset/Serializable.h` - Abstract base class: Key/Save/Load virtual interface
- `src/asset/SceneComponent.h/cpp` - Scene serialization adapter (Serializable implementation)
- `src/asset/ConfigComponent.h/cpp` - RenderConfig serialization adapter (Serializable implementation)
- `src/render/buffers/Buffer.h` - Virtual base class (Buffer) with m_buffer, m_memory
- `src/render/buffers/StagingBuffer.h/cpp` - Host-visible staging buffer (StagingBuffer) for CPU↔GPU transfers
- `src/render/buffers/GPUBuffer.h/cpp` - Device-local GPU buffer (GPUBuffer) with staging Map/Unmap
- `src/render/buffers/UniformBuffer.h` - Template uniform buffer (UniformBuffer<T>) for host-visible struct upload
- `src/render/buffers/VertexBuffer.h/cpp` - Vertex buffer (VertexBuffer, inherits GPUBuffer)
- `src/render/buffers/IndexBuffer.h/cpp` - Index buffer (IndexBuffer, inherits GPUBuffer)
- `src/render/Image.h/cpp` - GPU image with ImageState tracking (including Invalid), FromImageData factory (returns shared_ptr), mipmap gen
- `src/render/Barrier.h/cpp` - Centralized image barrier: ImageState → Vulkan layout/stage/access (maps Invalid → Undefined)
- `src/render/Texture.h/cpp` - Texture resource (combines Image + sampler + descriptor)
- `src/render/DescriptorManager.h/cpp` - Descriptor pool/set lifecycle management
- `src/render/UploadManager.h/cpp` - CPU-to-GPU upload service (meshes, lights, environments, IBL)
- `src/render/resources/LightingGPU.h/cpp` - GPU-side light SSBO storage (point + sun, push constants)

## Core Responsibilities

1. **Mesh Data Loading** (`src/asset/MeshData.h/cpp`)
   - Parse OBJ files into vertex/index buffers
   - Compute vertex attributes (positions, normals, UVs, tangents)
   - Provide MeshData struct for GPU upload

2. **Image Data Loading** (`src/asset/ImageData.h/cpp`)
   - Decode PNG/BMP/HDR files into owned pixel buffers (std::vector&lt;uint8_t&gt;)
   - Owns width, height, and format metadata
   - Member functions: `SavePNG()` / `SaveHDR()` for disk output
   - Constructor from path auto-loads; CPU-side representation; GPU upload via `Image::FromImageData`

3. **Buffer Class Hierarchy** (`src/render/buffers/Buffer.h`)
   - Virtual base class `Buffer` with `m_buffer`, `m_memory`
   - `StagingBuffer` — host-visible transfers (owns m_queue)
   - `GPUBuffer` — device-local with staging-backed Map/Unmap
   - `UniformBuffer<T>` — host-visible template for uniform structs
   - Vertex/index buffers inherit from GPUBuffer

4. **GPU Image Abstraction** (`src/render/Image.h/cpp`)
   - Create `vk::raii::Image` with appropriate tiling, usage, memory
   - Create `vk::raii::ImageView` and `vk::raii::Sampler`
   - Track logical state via `ImageState` enum; all transitions through `Barrier::Transition`
   - Mipmap generation via `GenerateMipmaps()`

5. **Texture Management** (`src/render/Texture.h/cpp`)
   - Combines GPU Image + ImageView + Sampler into a single resource
   - Descriptor binding for shaders

6. **Descriptor Management** (`src/render/DescriptorManager.h/cpp`)
   - Pool-based descriptor allocation
   - Per-frame descriptor pool rotation
   - Descriptor set layout caching

7. **Project Serialization** (`src/asset/Project.h/cpp`)
   - Pure registration-based serializer; owns no data
   - Components register via `Register<T>(args...)` and implement `Serializable` base class
   - Save/Load iterates all registered components for persistence
   - No coupling to Scene, RenderConfig, or any concrete type

## Data Flow

```
File System (OBJ, PNG)
    │
    ▼
src/asset/: MeshData / ImageData (CPU-side loading)
    │                              ▲
    │  Image::FromImageData()      │  Image::ReadImageData()
    ▼                              │
src/render/: Image (GPU) ──────────┘
    │
    ▼
src/render/buffers/: Buffer / StagingBuffer / GPUBuffer / UniformBuffer (GPU buffers)
    │
    ▼
Renderer passes (GeometryPass, LightingPass): consume GPU resources
```

All image layout transitions go through `Barrier::Transition()`.

## Architectural Boundaries

### ✅ Asset & Resource Code MAY:
- Load and parse asset files (OBJ, PNG, HDR)
- Provide CPU-side data structs (MeshData, ImageData)
- Query pixel format metadata via `PixelFormat` enum (Vulkan-free)
- Own GPU memory allocations (VkDeviceMemory via Buffer, Image) -- Renderer layer only
- Provide allocation utilities to Renderer passes

### ❌ Asset & Resource Code MUST NOT:
- Include Vulkan headers in public interfaces (`src/asset/` is Vulkan-free)
- Issue draw calls (Renderer's responsibility)
- Create pipelines or shader modules (Renderer's responsibility)
- Manage swapchain or presentation (Renderer's responsibility)
- Depend on Editor or UI layers
- Store application-level state

## Current Scope

- OBJ mesh loading via MeshData (icosphere, cube, etc.)
- PNG/HDR image decoding via ImageData (owns pixel data, member SavePNG/SaveHDR)
- `PixelFormat` enum for CPU-side format queries (PixelByteSize, ChannelCount, IsSRGB, IsHDR) -- zero Vulkan includes
- Buffer hierarchy (Buffer, StagingBuffer, GPUBuffer, UniformBuffer<T>) for vertex, index, uniform, and storage buffers
- Image for GPU image allocation with ImageState tracking (including Invalid) and Barrier::Transition for layout changes; FromImageData returns shared_ptr<Image>
- Texture class combining image + sampler + descriptor
- Barrier for centralized image barrier management (ImageState → Vulkan layout/stage/access, maps Invalid → Undefined)
- DescriptorManager with per-frame descriptor pool rotation
- UploadManager for CPU-to-GPU uploads (meshes, lights, environments, IBL cubemaps)
- LightingGPU for point/sun light SSBO management (owned by RenderCache)
- Project: pure registration-based serializer (no data ownership) with Serializable base class,
  SceneComponent, and ConfigComponent adapter components
- RenderCache (renderer-owned): cross-frame mutable resource pool with lazy attachment creation (`GetAttachment(name, extent)`), per-light shadow map management (`GetShadowMap(lightUID, lightType)` supporting `LightType::POINTLIGHT` cubemap and `LightType::SUNLIGHT` 2D orthographic), a shared layered shadow intensity array (`GetShadowIntensityArray(extent)` with per-light layer indices via `GetShadowIntensityLayer(lightUID, extent)`), cross-frame GPU resources for meshes (`MeshGPU` via `GetMeshGPU()`) and environments (`EnvironmentGPU` via `CreateEnvironmentGPU()`), and LightingGPU for light SSBOs (`InitLightingGPU()`, `GetLightingGPU()`, `UpdateLighting(variantDict)`). The `m_shadowMaps` map stores both `vk::ImageType::eCube` (point) and `vk::ImageType::e2D` (sun) `Image` instances by light UID.

## Future Enhancements

- glTF 2.0 loader with PBR material support
- Texture compression (BCn, ASTC)
- Async asset loading thread pool
- Vulkan Memory Allocator (VMA) integration
- Pipeline cache serialization
