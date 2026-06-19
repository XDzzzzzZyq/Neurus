# Graph Report - .  (2026-06-19)

## Corpus Check
- 109 files · ~53,947 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 742 nodes · 1025 edges · 81 communities (48 shown, 33 thin omitted)
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS · INFERRED: 4 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Renderer - Sync|Renderer - Sync]]
- [[_COMMUNITY_UI Layer|UI Layer]]
- [[_COMMUNITY_Scene - Transform|Scene - Transform]]
- [[_COMMUNITY_Renderer - Descriptors|Renderer - Descriptors]]
- [[_COMMUNITY_Renderer - TextureImage|Renderer - Texture/Image]]
- [[_COMMUNITY_Renderer - TextureImage|Renderer - Texture/Image]]
- [[_COMMUNITY_Renderer - TextureImage|Renderer - Texture/Image]]
- [[_COMMUNITY_Editor - Input|Editor - Input]]
- [[_COMMUNITY_Editor - Selection|Editor - Selection]]
- [[_COMMUNITY_Renderer - Attachment|Renderer - Attachment]]
- [[_COMMUNITY_Renderer - Swapchain|Renderer - Swapchain]]
- [[_COMMUNITY_Renderer - Descriptors|Renderer - Descriptors]]
- [[_COMMUNITY_Asset - Meshes|Asset - Meshes]]
- [[_COMMUNITY_Renderer - Descriptors|Renderer - Descriptors]]
- [[_COMMUNITY_Renderer - TextureImage|Renderer - Texture/Image]]
- [[_COMMUNITY_Editor - Context|Editor - Context]]
- [[_COMMUNITY_Scene - Light|Scene - Light]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Buffers|Renderer - Buffers]]
- [[_COMMUNITY_Asset - Images|Asset - Images]]
- [[_COMMUNITY_Renderer - Attachment|Renderer - Attachment]]
- [[_COMMUNITY_Renderer - TextureImage|Renderer - Texture/Image]]
- [[_COMMUNITY_Scene - Camera|Scene - Camera]]
- [[_COMMUNITY_Scene - Scene Graph|Scene - Scene Graph]]
- [[_COMMUNITY_Editor - Context|Editor - Context]]
- [[_COMMUNITY_Renderer - Pipeline|Renderer - Pipeline]]
- [[_COMMUNITY_Scene - Mesh|Scene - Mesh]]
- [[_COMMUNITY_Editor - Controllers|Editor - Controllers]]
- [[_COMMUNITY_Renderer - Descriptors|Renderer - Descriptors]]
- [[_COMMUNITY_Project System - Project|Project System - Project]]
- [[_COMMUNITY_Renderer - Command|Renderer - Command]]
- [[_COMMUNITY_Scene - Debug|Scene - Debug]]
- [[_COMMUNITY_Scene - Debug|Scene - Debug]]
- [[_COMMUNITY_Renderer - Pipeline|Renderer - Pipeline]]
- [[_COMMUNITY_Renderer - Buffers|Renderer - Buffers]]
- [[_COMMUNITY_Renderer - Buffers|Renderer - Buffers]]
- [[_COMMUNITY_Renderer - Pipeline|Renderer - Pipeline]]
- [[_COMMUNITY_Application Core - App|Application Core - App]]
- [[_COMMUNITY_Editor - Events|Editor - Events]]
- [[_COMMUNITY_Renderer - Pipeline|Renderer - Pipeline]]
- [[_COMMUNITY_Scene - Sprite|Scene - Sprite]]
- [[_COMMUNITY_Renderer - Pipeline|Renderer - Pipeline]]
- [[_COMMUNITY_UI Layer|UI Layer]]
- [[_COMMUNITY_Asset|Asset]]
- [[_COMMUNITY_Asset|Asset]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Core|Core]]
- [[_COMMUNITY_Core|Core]]
- [[_COMMUNITY_Editor - Controllers|Editor - Controllers]]
- [[_COMMUNITY_Editor|Editor]]
- [[_COMMUNITY_Editor - Selection|Editor - Selection]]
- [[_COMMUNITY_Editor - Events|Editor - Events]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Passes|Renderer - Passes]]
- [[_COMMUNITY_Renderer - Passes|Renderer - Passes]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Scene|Scene]]
- [[_COMMUNITY_Scene|Scene]]
- [[_COMMUNITY_Scene|Scene]]
- [[_COMMUNITY_Scene|Scene]]
- [[_COMMUNITY_Scene|Scene]]
- [[_COMMUNITY_Renderer - Render|Renderer - Render]]
- [[_COMMUNITY_Renderer - Pipeline|Renderer - Pipeline]]
- [[_COMMUNITY_Renderer - Pipeline|Renderer - Pipeline]]
- [[_COMMUNITY_Project System - Project|Project System - Project]]
- [[_COMMUNITY_UI Layer|UI Layer]]
- [[_COMMUNITY_UI Layer|UI Layer]]

## God Nodes (most connected - your core abstractions)
1. `BeginPass()` - 13 edges
2. `Image()` - 12 edges
3. `createFromPixelData()` - 12 edges
4. `LoadObjFromString()` - 11 edges
5. `SavePixelData()` - 10 edges
6. `ReadImageToBuffer()` - 10 edges
7. `FromFile()` - 10 edges
8. `VulkanImage()` - 10 edges
9. `ReadImageToBuffer()` - 10 edges
10. `AddFace()` - 9 edges

## Surprising Connections (you probably didn't know these)
- `NeurusMainWindow()` --calls--> `Resize()`  [INFERRED]
  src/ui/NeurusMainWindow.cpp → src/render/AttachmentManager.cpp
- `CaptureAllAttachments()` --calls--> `AttachmentNameToString()`  [INFERRED]
  src/render/Screenshot.cpp → src/render/AttachmentManager.cpp
- `CreateDescriptorSetLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/LightingPass.cpp → src/render/DescriptorManager.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/GeometryPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (81 total, 33 thin omitted)

### Community 0 - "Renderer - Sync"
Cohesion: 0.06
Nodes (38): CommandPool, Fence, Mesh, buildRenderItem(), computeCameraData(), createCommandPool(), DeferredRenderer(), DrawFrame() (+30 more)

### Community 1 - "UI Layer"
Cohesion: 0.08
Nodes (32): resolveResourcePath(), Run(), QKeyEvent, QMouseEvent, QPaintEvent, QResizeEvent, QWheelEvent, QString (+24 more)

### Community 2 - "Scene - Transform"
Cohesion: 0.07
Nodes (20): mutable, PointType, SetColor(), SetColor(), SetPointType(), cereal(), neurus(), GetTransformPtr() (+12 more)

### Community 3 - "Renderer - Descriptors"
Cohesion: 0.09
Nodes (25): neurus(), neurus(), CreateDescriptorSetLayout(), CreatePipeline(), CreateSampler(), GetLightSSBO(), LightingPass(), Record() (+17 more)

### Community 4 - "Renderer - Texture/Image"
Cohesion: 0.18
Nodes (24): computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData(), FromFile(), LoadTexture(), pixelByteSize() (+16 more)

### Community 5 - "Renderer - Texture/Image"
Cohesion: 0.15
Nodes (26): AccessFlagsForLayout(), allocateAndBindMemory(), AspectFromFormat(), createImage(), createImageView(), FindMemoryType(), GenerateMipmaps(), PipelineStageForLayout() (+18 more)

### Community 6 - "Renderer - Texture/Image"
Cohesion: 0.16
Nodes (25): AccessFlagsForLayout(), allocateAndBindMemory(), AspectFromFormat(), createImage(), createImageView(), FindMemoryType(), GenerateMipmaps(), Image() (+17 more)

### Community 7 - "Editor - Input"
Cohesion: 0.10
Nodes (10): GetInputState(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld(), RecordMousePress(), RecordMouseRelease() (+2 more)

### Community 8 - "Editor - Selection"
Cohesion: 0.13
Nodes (15): BoxSelect(), RaycastSelect(), RayIntersectsSphere(), ScreenToRay(), neurus(), EventBus(), neurus(), Ray (+7 more)

### Community 9 - "Renderer - Attachment"
Cohesion: 0.18
Nodes (20): AttachmentLoadOp, AttachmentStoreOp, ClearValue, PassType, BeginPass(), ColorAttachmentCount(), colorLoadOpFor(), colorStoreOpFor() (+12 more)

### Community 10 - "Renderer - Swapchain"
Cohesion: 0.18
Nodes (20): PresentModeKHR, AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate() (+12 more)

### Community 11 - "Renderer - Descriptors"
Cohesion: 0.13
Nodes (18): DescriptorSetLayoutBuilder, BuildLayout(), CreateCameraLayout(), CreatePipeline(), GeometryPass(), Record(), RenderPassManager, AttachmentManager (+10 more)

### Community 12 - "Asset - Meshes"
Cohesion: 0.28
Nodes (17): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetMeshCenter(), GetMeshName(), GetVertexCount() (+9 more)

### Community 13 - "Renderer - Descriptors"
Cohesion: 0.21
Nodes (15): DescriptorImageInfo, DescriptorPoolSize, DescriptorSetLayoutBinding, DescriptorType, Allocate(), Build(), CalculatePoolSizes(), DescriptorPool() (+7 more)

### Community 14 - "Renderer - Texture/Image"
Cohesion: 0.22
Nodes (15): ImageData, neurus(), CaptureAllAttachments(), CaptureAttachment(), CaptureSwapchain(), timestampedFilename(), namespace, AttachmentManager (+7 more)

### Community 15 - "Editor - Context"
Cohesion: 0.17
Nodes (13): activeScene(), GetActiveCamera(), GetObjectID(), GetObjectIDs(), SetScene(), Camera, Context(), EditorContext() (+5 more)

### Community 16 - "Scene - Light"
Cohesion: 0.15
Nodes (8): LightType, pair, Light(), ParseLightName(), SetColor(), SpriteType, string, vec3

### Community 17 - "Renderer - Render"
Cohesion: 0.19
Nodes (14): CreateInstance(), debugCallback(), findGraphicsQueueFamily(), getRequiredInstanceExtensions(), initDevice(), selectPhysicalDeviceIndex(), VulkanContext(), Instance (+6 more)

### Community 18 - "Renderer - Buffers"
Cohesion: 0.22
Nodes (11): BufferUsageFlags, findMemoryType(), GetDescriptorInfo(), Upload(), VulkanBuffer(), DescriptorBufferInfo, Device, DeviceSize (+3 more)

### Community 19 - "Asset - Images"
Cohesion: 0.31
Nodes (12): ChannelCount(), ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), IsBGRFormat(), PixelByteSize(), SavePixelData(), SwizzleBGRtoRGB() (+4 more)

### Community 20 - "Renderer - Attachment"
Cohesion: 0.27
Nodes (12): AttachmentConfig, AttachmentName, AttachmentManager(), AttachmentNameToString(), ConfigFor(), Create(), createAttachment(), HasAttachment() (+4 more)

### Community 21 - "Renderer - Texture/Image"
Cohesion: 0.21
Nodes (11): MatDataType, MaterialRes, MatParaType, InitParamData(), LoadMaterial(), Material(), ParseConfig(), SetMatParam() (+3 more)

### Community 22 - "Scene - Camera"
Cohesion: 0.21
Nodes (6): GetProjectionMatrix(), GetViewMatrix(), SetCamPos(), SetTarPos(), mat4, vec3

### Community 23 - "Scene - Scene Graph"
Cohesion: 0.18
Nodes (6): CheckStatus(), GetActiveCamera(), GetObjectID(), SceneModifStatus, Camera, ObjectID

### Community 24 - "Editor - Context"
Cohesion: 0.24
Nodes (8): _Base, neurus(), RenderContext(), hovered_object(), class, Context(), EditorContext(), namespace

### Community 25 - "Renderer - Pipeline"
Cohesion: 0.31
Nodes (9): PipelineShaderStageCreateInfo, FromEmbedded(), FromFile(), GetStageInfo(), ShaderModule(), ShaderStageFlagBits, Device, string (+1 more)

### Community 26 - "Scene - Mesh"
Cohesion: 0.22
Nodes (7): Mesh(), ReloadMeshData(), UploadToGPU(), Device, PhysicalDevice, Queue, string

### Community 27 - "Editor - Controllers"
Cohesion: 0.58
Nodes (8): GetSpeedMultiplier(), Orbit(), Pan(), Update(), Zoom(), Camera, InputState, Scene

### Community 28 - "Renderer - Descriptors"
Cohesion: 0.48
Nodes (6): AddAttribute(), GetBindingDescription(), GetFormatSize(), GetStride(), Format, VertexInputBindingDescription

### Community 29 - "Project System - Project"
Cohesion: 0.57
Nodes (6): CreateDefault(), New(), Open(), Save(), Project(), string

### Community 30 - "Renderer - Command"
Cohesion: 0.33
Nodes (5): neurus(), SetScissor(), SetViewport(), namespace, vk

### Community 31 - "Scene - Debug"
Cohesion: 0.38
Nodes (4): PushDebugLine(), PushDebugLines(), vec3, vector

### Community 32 - "Scene - Debug"
Cohesion: 0.38
Nodes (4): PushDebugPoint(), PushDebugPoints(), vec3, vector

### Community 33 - "Renderer - Pipeline"
Cohesion: 0.33
Nodes (5): ShaderModule, LoadShader(), Device, shared_ptr, string

### Community 34 - "Renderer - Buffers"
Cohesion: 0.40
Nodes (5): IndexBuffer(), Device, DeviceSize, PhysicalDevice, Queue

### Community 35 - "Renderer - Buffers"
Cohesion: 0.40
Nodes (5): VertexBuffer(), Device, DeviceSize, PhysicalDevice, Queue

### Community 36 - "Renderer - Pipeline"
Cohesion: 0.50
Nodes (4): BuildComputePipeline(), ComputePipelineBuilder(), Device, Pipeline

### Community 37 - "Application Core - App"
Cohesion: 0.67
Nodes (3): neurus(), project(), namespace

### Community 38 - "Editor - Events"
Cohesion: 0.83
Nodes (3): gpuName(), setGpuName(), QString

### Community 39 - "Renderer - Pipeline"
Cohesion: 0.67
Nodes (3): BuildGraphicsPipeline(), Device, Pipeline

### Community 41 - "Renderer - Pipeline"
Cohesion: 0.67
Nodes (3): ShaderProgram(), Device, Extent2D

### Community 42 - "UI Layer"
Cohesion: 0.67
Nodes (3): namespace, ads(), neurus()

## Knowledge Gaps
- **185 isolated node(s):** `QString`, `vector`, `Extent2D`, `namespace`, `namespace` (+180 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **33 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ImageData` connect `Renderer - Texture/Image` to `Renderer - Texture/Image`, `Renderer - Texture/Image`?**
  _High betweenness centrality (0.013) - this node is a cross-community bridge._
- **Why does `Texture` connect `Renderer - Texture/Image` to `UI Layer`?**
  _High betweenness centrality (0.009) - this node is a cross-community bridge._
- **Why does `Image()` connect `Renderer - Texture/Image` to `Renderer - Texture/Image`?**
  _High betweenness centrality (0.009) - this node is a cross-community bridge._
- **What connects `QString`, `vector`, `Extent2D` to the rest of the system?**
  _185 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Renderer - Sync` be split into smaller, more focused modules?**
  _Cohesion score 0.057971014492753624 - nodes in this community are weakly interconnected._
- **Should `UI Layer` be split into smaller, more focused modules?**
  _Cohesion score 0.08392603129445235 - nodes in this community are weakly interconnected._
- **Should `Scene - Transform` be split into smaller, more focused modules?**
  _Cohesion score 0.06554621848739496 - nodes in this community are weakly interconnected._