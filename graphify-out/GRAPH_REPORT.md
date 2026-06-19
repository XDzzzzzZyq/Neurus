# Graph Report - .  (2026-06-19)

## Corpus Check
- 109 files · ~53,947 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 742 nodes · 1025 edges · 81 communities (48 shown, 33 thin omitted)
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS · INFERRED: 4 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Deferred Renderer Core|Deferred Renderer Core]]
- [[_COMMUNITY_Application & Window Events|Application & Window Events]]
- [[_COMMUNITY_Scene Debug Visualization|Scene Debug Visualization]]
- [[_COMMUNITY_Lighting Pass & IndexVertex Buffers|Lighting Pass & Index/Vertex Buffers]]
- [[_COMMUNITY_Texture & Material Cache|Texture & Material Cache]]
- [[_COMMUNITY_Vulkan Image GPU Resource|Vulkan Image GPU Resource]]
- [[_COMMUNITY_Render Image Abstraction|Render Image Abstraction]]
- [[_COMMUNITY_Editor Input System|Editor Input System]]
- [[_COMMUNITY_Selection Controller|Selection Controller]]
- [[_COMMUNITY_Render Pass & Attachment Config|Render Pass & Attachment Config]]
- [[_COMMUNITY_Swapchain & Presentation|Swapchain & Presentation]]
- [[_COMMUNITY_Geometry Pass & Descriptor Layout|Geometry Pass & Descriptor Layout]]
- [[_COMMUNITY_Mesh Data Loading (OBJ)|Mesh Data Loading (OBJ)]]
- [[_COMMUNITY_Descriptor Pool Management|Descriptor Pool Management]]
- [[_COMMUNITY_Screenshot Capture & Image Data|Screenshot Capture & Image Data]]
- [[_COMMUNITY_Editor Context & Scene State|Editor Context & Scene State]]
- [[_COMMUNITY_Light Component System|Light Component System]]
- [[_COMMUNITY_Vulkan Context Initialization|Vulkan Context Initialization]]
- [[_COMMUNITY_GPU Buffer Abstraction|GPU Buffer Abstraction]]
- [[_COMMUNITY_Image Data Asset Loading|Image Data Asset Loading]]
- [[_COMMUNITY_Attachment Manager|Attachment Manager]]
- [[_COMMUNITY_Material Shader Parameters|Material Shader Parameters]]
- [[_COMMUNITY_Camera System|Camera System]]
- [[_COMMUNITY_Scene Graph Management|Scene Graph Management]]
- [[_COMMUNITY_Editor Core State|Editor Core State]]
- [[_COMMUNITY_Shader Module & Pipeline Stages|Shader Module & Pipeline Stages]]
- [[_COMMUNITY_Scene Mesh GPU Upload|Scene Mesh GPU Upload]]
- [[_COMMUNITY_Camera Controller (OrbitPanZoom)|Camera Controller (Orbit/Pan/Zoom)]]
- [[_COMMUNITY_Buffer Layout Attributes|Buffer Layout Attributes]]
- [[_COMMUNITY_Project SaveLoad|Project Save/Load]]
- [[_COMMUNITY_Command Buffer Wrapper|Command Buffer Wrapper]]
- [[_COMMUNITY_Debug Line Rendering|Debug Line Rendering]]
- [[_COMMUNITY_Debug Points Rendering|Debug Points Rendering]]
- [[_COMMUNITY_Shader Library Cache|Shader Library Cache]]
- [[_COMMUNITY_Index Buffer GPU Resource|Index Buffer GPU Resource]]
- [[_COMMUNITY_Vertex Buffer GPU Resource|Vertex Buffer GPU Resource]]
- [[_COMMUNITY_Compute Pipeline Builder|Compute Pipeline Builder]]
- [[_COMMUNITY_Application Class Header|Application Class Header]]
- [[_COMMUNITY_UI Events GPU Name|UI Events GPU Name]]
- [[_COMMUNITY_Graphics Pipeline Builder|Graphics Pipeline Builder]]
- [[_COMMUNITY_Sprite Component|Sprite Component]]
- [[_COMMUNITY_Shader Program Pipeline|Shader Program Pipeline]]
- [[_COMMUNITY_Neurus Main Window|Neurus Main Window]]
- [[_COMMUNITY_Image Data Header|Image Data Header]]
- [[_COMMUNITY_Mesh Data Header|Mesh Data Header]]
- [[_COMMUNITY_Buffer Layout Header|Buffer Layout Header]]
- [[_COMMUNITY_Core Parameters|Core Parameters]]
- [[_COMMUNITY_Core Types|Core Types]]
- [[_COMMUNITY_Camera Controller Header|Camera Controller Header]]
- [[_COMMUNITY_Input Header|Input Header]]
- [[_COMMUNITY_Selection Controller Header|Selection Controller Header]]
- [[_COMMUNITY_UI Events Header|UI Events Header]]
- [[_COMMUNITY_Attachment Manager Header|Attachment Manager Header]]
- [[_COMMUNITY_Compute Pipeline Builder Header|Compute Pipeline Builder Header]]
- [[_COMMUNITY_Deferred Renderer Header|Deferred Renderer Header]]
- [[_COMMUNITY_Descriptor Manager Header|Descriptor Manager Header]]
- [[_COMMUNITY_Geometry Pass Header|Geometry Pass Header]]
- [[_COMMUNITY_Lighting Pass Header|Lighting Pass Header]]
- [[_COMMUNITY_Pipeline Builder Header|Pipeline Builder Header]]
- [[_COMMUNITY_Screenshot Header|Screenshot Header]]
- [[_COMMUNITY_Swapchain Header|Swapchain Header]]
- [[_COMMUNITY_Texture Header|Texture Header]]
- [[_COMMUNITY_Vulkan Buffer Header|Vulkan Buffer Header]]
- [[_COMMUNITY_Vulkan Context Header|Vulkan Context Header]]
- [[_COMMUNITY_Scene Camera Header|Scene Camera Header]]
- [[_COMMUNITY_Default Scene Header|Default Scene Header]]
- [[_COMMUNITY_Scene Header|Scene Header]]
- [[_COMMUNITY_Sprite Header|Sprite Header]]
- [[_COMMUNITY_UID Header|UID Header]]
- [[_COMMUNITY_Shader Library Header|Shader Library Header]]
- [[_COMMUNITY_Shader Module Header|Shader Module Header]]
- [[_COMMUNITY_Shader Program Header|Shader Program Header]]
- [[_COMMUNITY_Project Header|Project Header]]
- [[_COMMUNITY_Main Window Header|Main Window Header]]
- [[_COMMUNITY_Vulkan Widget Header|Vulkan Widget Header]]

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

### Community 0 - "Deferred Renderer Core"
Cohesion: 0.06
Nodes (38): CommandPool, Fence, Mesh, buildRenderItem(), computeCameraData(), createCommandPool(), DeferredRenderer(), DrawFrame() (+30 more)

### Community 1 - "Application & Window Events"
Cohesion: 0.08
Nodes (32): resolveResourcePath(), Run(), QKeyEvent, QMouseEvent, QPaintEvent, QResizeEvent, QWheelEvent, QString (+24 more)

### Community 2 - "Scene Debug Visualization"
Cohesion: 0.07
Nodes (20): mutable, PointType, SetColor(), SetColor(), SetPointType(), cereal(), neurus(), GetTransformPtr() (+12 more)

### Community 3 - "Lighting Pass & Index/Vertex Buffers"
Cohesion: 0.09
Nodes (25): neurus(), neurus(), CreateDescriptorSetLayout(), CreatePipeline(), CreateSampler(), GetLightSSBO(), LightingPass(), Record() (+17 more)

### Community 4 - "Texture & Material Cache"
Cohesion: 0.18
Nodes (24): computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData(), FromFile(), LoadTexture(), pixelByteSize() (+16 more)

### Community 5 - "Vulkan Image GPU Resource"
Cohesion: 0.15
Nodes (26): AccessFlagsForLayout(), allocateAndBindMemory(), AspectFromFormat(), createImage(), createImageView(), FindMemoryType(), GenerateMipmaps(), PipelineStageForLayout() (+18 more)

### Community 6 - "Render Image Abstraction"
Cohesion: 0.16
Nodes (25): AccessFlagsForLayout(), allocateAndBindMemory(), AspectFromFormat(), createImage(), createImageView(), FindMemoryType(), GenerateMipmaps(), Image() (+17 more)

### Community 7 - "Editor Input System"
Cohesion: 0.10
Nodes (10): GetInputState(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld(), RecordMousePress(), RecordMouseRelease() (+2 more)

### Community 8 - "Selection Controller"
Cohesion: 0.13
Nodes (15): BoxSelect(), RaycastSelect(), RayIntersectsSphere(), ScreenToRay(), neurus(), EventBus(), neurus(), Ray (+7 more)

### Community 9 - "Render Pass & Attachment Config"
Cohesion: 0.18
Nodes (20): AttachmentLoadOp, AttachmentStoreOp, ClearValue, PassType, BeginPass(), ColorAttachmentCount(), colorLoadOpFor(), colorStoreOpFor() (+12 more)

### Community 10 - "Swapchain & Presentation"
Cohesion: 0.18
Nodes (20): PresentModeKHR, AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate() (+12 more)

### Community 11 - "Geometry Pass & Descriptor Layout"
Cohesion: 0.13
Nodes (18): DescriptorSetLayoutBuilder, BuildLayout(), CreateCameraLayout(), CreatePipeline(), GeometryPass(), Record(), RenderPassManager, AttachmentManager (+10 more)

### Community 12 - "Mesh Data Loading (OBJ)"
Cohesion: 0.28
Nodes (17): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetMeshCenter(), GetMeshName(), GetVertexCount() (+9 more)

### Community 13 - "Descriptor Pool Management"
Cohesion: 0.21
Nodes (15): DescriptorImageInfo, DescriptorPoolSize, DescriptorSetLayoutBinding, DescriptorType, Allocate(), Build(), CalculatePoolSizes(), DescriptorPool() (+7 more)

### Community 14 - "Screenshot Capture & Image Data"
Cohesion: 0.22
Nodes (15): ImageData, neurus(), CaptureAllAttachments(), CaptureAttachment(), CaptureSwapchain(), timestampedFilename(), namespace, AttachmentManager (+7 more)

### Community 15 - "Editor Context & Scene State"
Cohesion: 0.17
Nodes (13): activeScene(), GetActiveCamera(), GetObjectID(), GetObjectIDs(), SetScene(), Camera, Context(), EditorContext() (+5 more)

### Community 16 - "Light Component System"
Cohesion: 0.15
Nodes (8): LightType, pair, Light(), ParseLightName(), SetColor(), SpriteType, string, vec3

### Community 17 - "Vulkan Context Initialization"
Cohesion: 0.19
Nodes (14): CreateInstance(), debugCallback(), findGraphicsQueueFamily(), getRequiredInstanceExtensions(), initDevice(), selectPhysicalDeviceIndex(), VulkanContext(), Instance (+6 more)

### Community 18 - "GPU Buffer Abstraction"
Cohesion: 0.22
Nodes (11): BufferUsageFlags, findMemoryType(), GetDescriptorInfo(), Upload(), VulkanBuffer(), DescriptorBufferInfo, Device, DeviceSize (+3 more)

### Community 19 - "Image Data Asset Loading"
Cohesion: 0.31
Nodes (12): ChannelCount(), ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), IsBGRFormat(), PixelByteSize(), SavePixelData(), SwizzleBGRtoRGB() (+4 more)

### Community 20 - "Attachment Manager"
Cohesion: 0.27
Nodes (12): AttachmentConfig, AttachmentName, AttachmentManager(), AttachmentNameToString(), ConfigFor(), Create(), createAttachment(), HasAttachment() (+4 more)

### Community 21 - "Material Shader Parameters"
Cohesion: 0.21
Nodes (11): MatDataType, MaterialRes, MatParaType, InitParamData(), LoadMaterial(), Material(), ParseConfig(), SetMatParam() (+3 more)

### Community 22 - "Camera System"
Cohesion: 0.21
Nodes (6): GetProjectionMatrix(), GetViewMatrix(), SetCamPos(), SetTarPos(), mat4, vec3

### Community 23 - "Scene Graph Management"
Cohesion: 0.18
Nodes (6): CheckStatus(), GetActiveCamera(), GetObjectID(), SceneModifStatus, Camera, ObjectID

### Community 24 - "Editor Core State"
Cohesion: 0.24
Nodes (8): _Base, neurus(), RenderContext(), hovered_object(), class, Context(), EditorContext(), namespace

### Community 25 - "Shader Module & Pipeline Stages"
Cohesion: 0.31
Nodes (9): PipelineShaderStageCreateInfo, FromEmbedded(), FromFile(), GetStageInfo(), ShaderModule(), ShaderStageFlagBits, Device, string (+1 more)

### Community 26 - "Scene Mesh GPU Upload"
Cohesion: 0.22
Nodes (7): Mesh(), ReloadMeshData(), UploadToGPU(), Device, PhysicalDevice, Queue, string

### Community 27 - "Camera Controller (Orbit/Pan/Zoom)"
Cohesion: 0.58
Nodes (8): GetSpeedMultiplier(), Orbit(), Pan(), Update(), Zoom(), Camera, InputState, Scene

### Community 28 - "Buffer Layout Attributes"
Cohesion: 0.48
Nodes (6): AddAttribute(), GetBindingDescription(), GetFormatSize(), GetStride(), Format, VertexInputBindingDescription

### Community 29 - "Project Save/Load"
Cohesion: 0.57
Nodes (6): CreateDefault(), New(), Open(), Save(), Project(), string

### Community 30 - "Command Buffer Wrapper"
Cohesion: 0.33
Nodes (5): neurus(), SetScissor(), SetViewport(), namespace, vk

### Community 31 - "Debug Line Rendering"
Cohesion: 0.38
Nodes (4): PushDebugLine(), PushDebugLines(), vec3, vector

### Community 32 - "Debug Points Rendering"
Cohesion: 0.38
Nodes (4): PushDebugPoint(), PushDebugPoints(), vec3, vector

### Community 33 - "Shader Library Cache"
Cohesion: 0.33
Nodes (5): ShaderModule, LoadShader(), Device, shared_ptr, string

### Community 34 - "Index Buffer GPU Resource"
Cohesion: 0.40
Nodes (5): IndexBuffer(), Device, DeviceSize, PhysicalDevice, Queue

### Community 35 - "Vertex Buffer GPU Resource"
Cohesion: 0.40
Nodes (5): VertexBuffer(), Device, DeviceSize, PhysicalDevice, Queue

### Community 36 - "Compute Pipeline Builder"
Cohesion: 0.50
Nodes (4): BuildComputePipeline(), ComputePipelineBuilder(), Device, Pipeline

### Community 37 - "Application Class Header"
Cohesion: 0.67
Nodes (3): neurus(), project(), namespace

### Community 38 - "UI Events GPU Name"
Cohesion: 0.83
Nodes (3): gpuName(), setGpuName(), QString

### Community 39 - "Graphics Pipeline Builder"
Cohesion: 0.67
Nodes (3): BuildGraphicsPipeline(), Device, Pipeline

### Community 41 - "Shader Program Pipeline"
Cohesion: 0.67
Nodes (3): ShaderProgram(), Device, Extent2D

### Community 42 - "Neurus Main Window"
Cohesion: 0.67
Nodes (3): namespace, ads(), neurus()

## Knowledge Gaps
- **185 isolated node(s):** `QString`, `vector`, `Extent2D`, `namespace`, `namespace` (+180 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **33 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ImageData` connect `Screenshot Capture & Image Data` to `Texture & Material Cache`, `Render Image Abstraction`?**
  _High betweenness centrality (0.013) - this node is a cross-community bridge._
- **Why does `Texture` connect `Texture & Material Cache` to `Application & Window Events`?**
  _High betweenness centrality (0.009) - this node is a cross-community bridge._
- **Why does `Image()` connect `Render Image Abstraction` to `Screenshot Capture & Image Data`?**
  _High betweenness centrality (0.009) - this node is a cross-community bridge._
- **What connects `QString`, `vector`, `Extent2D` to the rest of the system?**
  _185 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Deferred Renderer Core` be split into smaller, more focused modules?**
  _Cohesion score 0.057971014492753624 - nodes in this community are weakly interconnected._
- **Should `Application & Window Events` be split into smaller, more focused modules?**
  _Cohesion score 0.08392603129445235 - nodes in this community are weakly interconnected._
- **Should `Scene Debug Visualization` be split into smaller, more focused modules?**
  _Cohesion score 0.06554621848739496 - nodes in this community are weakly interconnected._