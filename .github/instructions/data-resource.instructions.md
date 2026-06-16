# Data & Resource Layer

## Overview

The Data & Resource layer manages **GPU resource lifecycles** and provides an
**asset pipeline** for loading external data. It abstracts Vulkan buffer/image
allocation, descriptor set management, and resource caching.

For the Triangle MVP, this layer is a **stub** - minimal abstractions that
establish the API contract without full implementation.

## Location

- `src/data/GPUResource.h` - Abstract GPU resource base class
- `src/data/Buffer.h` - Vulkan buffer + device memory abstraction (future)
- `src/data/Image.h` - Vulkan image + memory + image view abstraction (future)
- `src/data/DescriptorPool.h` - Descriptor set allocation (future)

## Core Responsibilities

1. **GPU Resource Abstraction** (in scope for MVP)
   - Base class `GPUResource` for non-copyable, RAII resource lifecycle
   - Establish the pattern: constructor allocates, destructor frees

2. **Memory Management** (future)
   - Buffer allocation with proper memory type selection
   - Image allocation with tiling and usage flags
   - Sub-allocation from larger memory blocks (VMA integration)

3. **Descriptor Management** (future)
   - Descriptor pool creation and set allocation
   - Descriptor set layout management
   - Uniform buffer, storage buffer, combined image sampler bindings

4. **Pipeline Cache** (future)
   - Pipeline cache serialization/deserialization
   - Faster startup by caching compiled pipelines

5. **Asset Pipeline** (future)
   - File format loaders (glTF, OBJ, PNG, HDR)
   - Texture compression and mipmap generation
   - Mesh data upload to GPU buffers
   - Async asset loading with callback notification

## Key Components

### GPUResource (Base Class)

```cpp
/**
 * @brief Abstract base class for all GPU-owned resources.
 *
 * Enforces RAII: resources are allocated in the derived constructor
 * and freed in the destructor. Copy semantics are deleted.
 *
 * Usage: inherit, implement allocation in constructor, cleanup in destructor.
 */
class GPUResource {
public:
    GPUResource() = default;
    virtual ~GPUResource() = default;

    GPUResource(const GPUResource&) = delete;
    GPUResource& operator=(const GPUResource&) = delete;

    GPUResource(GPUResource&&) = default;
    GPUResource& operator=(GPUResource&&) = default;

    /** @brief Returns true if the resource was successfully created. */
    virtual bool IsValid() const = 0;
};
```

### Buffer (Future)

```cpp
class Buffer : public GPUResource {
public:
    Buffer(const vk::raii::Device& device,
           vk::DeviceSize size,
           vk::BufferUsageFlags usage,
           vk::MemoryPropertyFlags memoryProperties);

    const vk::raii::Buffer& handle() const;
    const vk::raii::DeviceMemory& memory() const;
    vk::DeviceSize size() const;

    void Upload(const void* data, vk::DeviceSize size);
};
```

### Image (Future)

```cpp
class Image : public GPUResource {
public:
    Image(const vk::raii::Device& device,
          vk::Extent2D extent,
          vk::Format format,
          vk::ImageUsageFlags usage);

    const vk::raii::Image& handle() const;
    const vk::raii::DeviceMemory& memory() const;
    const vk::raii::ImageView& view() const;
    vk::Extent2D extent() const;
    vk::Format format() const;
};
```

## Memory Type Selection

Vulkan requires explicit memory type selection based on `vk::MemoryRequirements`
and `vk::MemoryPropertyFlags`. The Data layer encapsulates this:

```cpp
/**
 * @brief Find a suitable memory type index for the given requirements.
 * @param memoryTypeBits Bitmask from vk::MemoryRequirements::memoryTypeBits.
 * @param properties Desired memory properties (e.g., DEVICE_LOCAL, HOST_VISIBLE).
 * @return Index of a suitable memory type.
 * @throws std::runtime_error if no suitable type is found.
 */
uint32_t FindMemoryType(const vk::raii::PhysicalDevice& physicalDevice,
                        uint32_t memoryTypeBits,
                        vk::MemoryPropertyFlags properties);
```

## Future Architecture

### Asset Pipeline Flow

```
File System (glTF, OBJ, PNG)
    │
    ▼
Data Layer: File Parser
    │
    ▼
Data Layer: GPU Upload
    │ (Buffer::Upload, Image staging)
    ▼
Data Layer: Resource Cache
    │
    ▼
Renderer: Consumes resources
```

### Resource Cache

- Hash-based lookup for loaded assets
- Reference counting for shared resources
- LRU eviction for memory pressure
- Async loading with completion callbacks

## Architectural Boundaries

### ✅ Data & Resource MAY:
- Own GPU memory allocations (VkDeviceMemory)
- Own buffer and image handles (via RAII wrappers)
- Provide allocation utilities to Renderer
- Load and parse asset files (future)

### ❌ Data & Resource MUST NOT:
- Issue draw calls (Renderer's responsibility)
- Create pipelines or shader modules (Renderer's responsibility)
- Manage swapchain or presentation (Renderer's responsibility)
- Depend on Editor or UI layers
- Store application-level state

## Current Scope (Triangle MVP)

- `GPUResource` base class (abstract, enforces RAII contract)
- No concrete resource implementations yet (triangle uses no GPU buffers)
- No file loading or asset pipeline

## Future Enhancements

- Concrete Buffer and Image classes with Vulkan-HPP RAII
- Staging buffer for efficient CPU→GPU uploads
- Descriptor pool and descriptor set abstraction
- Vulkan Memory Allocator (VMA) integration
- glTF 2.0 loader with PBR material support
- Texture compression (BCn, ASTC)
- Async asset loading thread pool
