# Asset Layer & GPU Resource Management

## Overview

The Asset layer (`src/asset/`) manages **loading, storing, and providing access**
to external asset data (meshes, images). GPU resource abstractions (buffers,
images, descriptors) are owned by the **Renderer layer** (`src/render/`) — see
`renderer.instructions.md` for VulkanBuffer, VulkanImage, Texture, and
DescriptorManager.

The original "Data & Resource" layer concept has been absorbed: asset loading
lives in `src/asset/`, GPU resource management lives in `src/render/`.

## Location

- `src/asset/MeshData.h/cpp` - Mesh geometry data (vertices, indices)
- `src/asset/ImageData.h/cpp` - Image pixel data (CPU-side, raw or decoded)
- `src/render/VulkanBuffer.h/cpp` - GPU buffer abstraction (vertex, index, uniform, storage)
- `src/render/VulkanImage.h/cpp` - GPU image abstraction (allocation, views, transitions)
- `src/render/Image.h/cpp` - Higher-level image wrapper (loading, mipmaps)
- `src/render/Texture.h/cpp` - Texture resource (combines Image + sampler + descriptor)
- `src/render/DescriptorManager.h/cpp` - Descriptor pool/set lifecycle management

## Core Responsibilities

1. **Mesh Data Loading** (`src/asset/MeshData.h/cpp`)
   - Parse OBJ files into vertex/index buffers
   - Compute vertex attributes (positions, normals, UVs, tangents)
   - Provide MeshData struct for GPU upload

2. **Image Data Loading** (`src/asset/ImageData.h/cpp`)
   - Decode PNG/BMP/HDR files into raw pixel buffers
   - Provide ImageData struct with format, dimensions, pixel data
   - CPU-side representation; GPU upload handled by renderer

3. **GPU Buffer Abstraction** (`src/render/VulkanBuffer.h/cpp`)
   - Allocate device-local or host-visible `vk::raii::Buffer` + `vk::DeviceMemory`
   - Staging uploads from CPU to device-local memory
   - Vertex, index, uniform, and storage buffer types

4. **GPU Image Abstraction** (`src/render/VulkanImage.h/cpp`)
   - Create `vk::raii::Image` with appropriate tiling, usage, memory
   - Create `vk::raii::ImageView` and `vk::raii::Sampler`
   - Image layout transitions and mipmap generation

5. **Texture Management** (`src/render/Texture.h/cpp`)
   - Combines GPU Image + ImageView + Sampler into a single resource
   - Descriptor binding for shaders

6. **Descriptor Management** (`src/render/DescriptorManager.h/cpp`)
   - Pool-based descriptor allocation
   - Per-frame descriptor pool rotation
   - Descriptor set layout caching

## Data Flow

```
File System (OBJ, PNG)
    │
    ▼
src/asset/: MeshData / ImageData (CPU-side loading)
    │
    ▼
src/render/: VulkanBuffer / VulkanImage / Texture (GPU upload + resource)
    │
    ▼
Renderer passes (GeometryPass, LightingPass): consume GPU resources
```

## Architectural Boundaries

### ✅ Asset & Resource Code MAY:
- Load and parse asset files (OBJ, PNG, HDR)
- Provide CPU-side data structs (MeshData, ImageData)
- Own GPU memory allocations (VkDeviceMemory via VulkanBuffer, VulkanImage)
- Provide allocation utilities to Renderer passes

### ❌ Asset & Resource Code MUST NOT:
- Issue draw calls (Renderer's responsibility)
- Create pipelines or shader modules (Renderer's responsibility)
- Manage swapchain or presentation (Renderer's responsibility)
- Depend on Editor or UI layers
- Store application-level state

## Current Scope

- OBJ mesh loading via MeshData (icosphere, cube, etc.)
- PNG/HDR image decoding via ImageData
- VulkanBuffer for vertex, index, uniform, and storage buffers
- VulkanImage for GPU image allocation and layout transitions
- Texture class combining image + sampler + descriptor
- DescriptorManager with per-frame descriptor pool rotation

## Future Enhancements

- glTF 2.0 loader with PBR material support
- Texture compression (BCn, ASTC)
- Async asset loading thread pool
- Vulkan Memory Allocator (VMA) integration
- Pipeline cache serialization
