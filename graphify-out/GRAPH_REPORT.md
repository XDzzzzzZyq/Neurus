# Graph Report - Neurus  (2026-06-25)

## Corpus Check
- 117 files · ~68,915 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 960 nodes · 1426 edges · 78 communities (47 shown, 31 thin omitted)
- Extraction: 98% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 9 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `74b5d337`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

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
- [[_COMMUNITY_Editor Context & Scene State|Editor Context & Scene State]]
- [[_COMMUNITY_Light Component System|Light Component System]]
- [[_COMMUNITY_Vulkan Context Initialization|Vulkan Context Initialization]]
- [[_COMMUNITY_GPU Buffer Abstraction|GPU Buffer Abstraction]]
- [[_COMMUNITY_Image Data Asset Loading|Image Data Asset Loading]]
- [[_COMMUNITY_Attachment Manager|Attachment Manager]]
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
- [[_COMMUNITY_Shader Library Cache|Shader Library Cache]]
- [[_COMMUNITY_Index Buffer GPU Resource|Index Buffer GPU Resource]]
- [[_COMMUNITY_Vertex Buffer GPU Resource|Vertex Buffer GPU Resource]]
- [[_COMMUNITY_Compute Pipeline Builder|Compute Pipeline Builder]]
- [[_COMMUNITY_Graphics Pipeline Builder|Graphics Pipeline Builder]]
- [[_COMMUNITY_Sprite Component|Sprite Component]]
- [[_COMMUNITY_Shader Program Pipeline|Shader Program Pipeline]]
- [[_COMMUNITY_Neurus Main Window|Neurus Main Window]]
- [[_COMMUNITY_Core Parameters|Core Parameters]]
- [[_COMMUNITY_Core Types|Core Types]]
- [[_COMMUNITY_Camera Controller Header|Camera Controller Header]]
- [[_COMMUNITY_Input Header|Input Header]]
- [[_COMMUNITY_UI Events Header|UI Events Header]]
- [[_COMMUNITY_Attachment Manager Header|Attachment Manager Header]]
- [[_COMMUNITY_Compute Pipeline Builder Header|Compute Pipeline Builder Header]]
- [[_COMMUNITY_Deferred Renderer Header|Deferred Renderer Header]]
- [[_COMMUNITY_Descriptor Manager Header|Descriptor Manager Header]]
- [[_COMMUNITY_Geometry Pass Header|Geometry Pass Header]]
- [[_COMMUNITY_Lighting Pass Header|Lighting Pass Header]]
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
- [[_COMMUNITY_UID Implementation|UID Implementation]]
- [[_COMMUNITY_Shader Library Header|Shader Library Header]]
- [[_COMMUNITY_Shader Module Header|Shader Module Header]]
- [[_COMMUNITY_Shader Program Header|Shader Program Header]]
- [[_COMMUNITY_Project Header|Project Header]]
- [[_COMMUNITY_Main Window Header|Main Window Header]]
- [[_COMMUNITY_Vulkan Widget Header|Vulkan Widget Header]]
- [[_COMMUNITY_Light Spot Cutoff|Light Spot Cutoff]]
- [[_COMMUNITY_Editor Context Cpp|Editor Context Cpp]]
- [[_COMMUNITY_Community 81|Community 81]]
- [[_COMMUNITY_Community 85|Community 85]]
- [[_COMMUNITY_Community 86|Community 86]]
- [[_COMMUNITY_Community 87|Community 87]]
- [[_COMMUNITY_Community 88|Community 88]]
- [[_COMMUNITY_Community 89|Community 89]]
- [[_COMMUNITY_Community 90|Community 90]]

## God Nodes (most connected - your core abstractions)
1. `Image()` - 15 edges
2. `BeginPass()` - 13 edges
3. `createFromPixelData()` - 12 edges
4. `ReadImageToBuffer()` - 11 edges
5. `LoadObjFromString()` - 11 edges
6. `Initialize()` - 11 edges
7. `SavePixelData()` - 10 edges
8. `CaptureAllAttachments()` - 10 edges
9. `FromFile()` - 10 edges
10. `VulkanImage()` - 10 edges

## Surprising Connections (you probably didn't know these)
- `CreateDescriptorSetLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/LightingPass.cpp → src/render/DescriptorManager.cpp
- `CreateLightLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/ShadowDepthPass.cpp → src/render/DescriptorManager.cpp
- `CreateDescriptorSetLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/SSAOPass.cpp → src/render/DescriptorManager.cpp
- `ExportShadowDepthEquirect()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/DeferredRenderer.cpp → src/render/DescriptorManager.cpp
- `createUniforms()` --calls--> `DescriptorPool()`  [INFERRED]
  src/render/passes/ShadowDepthPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (78 total, 31 thin omitted)

### Community 0 - "Deferred Renderer Core"
Cohesion: 0.06
Nodes (38): CameraUBOData, CommandPool, neurus(), neurus(), Fence, GeometryRenderItem, m_fence(), m_semaphore() (+30 more)

### Community 1 - "Application & Window Events"
Cohesion: 0.10
Nodes (11): GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld(), RecordMousePress() (+3 more)

### Community 2 - "Scene Debug Visualization"
Cohesion: 0.06
Nodes (22): mutable, PointType, SetColor(), SetColor(), SetPointType(), neurus(), cereal(), neurus() (+14 more)

### Community 3 - "Lighting Pass & Index/Vertex Buffers"
Cohesion: 0.17
Nodes (14): HWND, QString, QWidget, Scene, CreateDocks(), CreateMenus(), getViewportHwnd(), getVulkanWidget() (+6 more)

### Community 4 - "Texture & Material Cache"
Cohesion: 0.19
Nodes (26): computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData(), FromFile(), FromImage(), LoadTexture() (+18 more)

### Community 5 - "Vulkan Image GPU Resource"
Cohesion: 0.15
Nodes (26): AccessFlagsForLayout(), allocateAndBindMemory(), AspectFromFormat(), createImage(), createImageView(), FindMemoryType(), GenerateMipmaps(), PipelineStageForLayout() (+18 more)

### Community 6 - "Render Image Abstraction"
Cohesion: 0.15
Nodes (31): AccessFlags, Format, ImageAspectFlags, ImageLayout, ImageType, ImageUsageFlags, MemoryPropertyFlags, PipelineStageFlags (+23 more)

### Community 7 - "Editor Input System"
Cohesion: 0.12
Nodes (16): BoxSelect(), RaycastSelect(), RayIntersectsSphere(), ScreenToRay(), EventQueue, neurus(), EventQueue(), GetEventQueue() (+8 more)

### Community 8 - "Selection Controller"
Cohesion: 0.17
Nodes (20): AttachmentLoadOp, AttachmentStoreOp, ClearValue, BeginPass(), ColorAttachmentCount(), colorLoadOpFor(), colorStoreOpFor(), depthLoadOpFor() (+12 more)

### Community 9 - "Render Pass & Attachment Config"
Cohesion: 0.18
Nodes (20): PresentModeKHR, AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate() (+12 more)

### Community 10 - "Swapchain & Presentation"
Cohesion: 0.28
Nodes (17): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetMeshCenter(), GetMeshName(), GetVertexCount() (+9 more)

### Community 11 - "Geometry Pass & Descriptor Layout"
Cohesion: 0.19
Nodes (17): DescriptorBindingFlags, DescriptorBufferInfo, DescriptorImageInfo, DescriptorPoolSize, DescriptorSetLayoutBinding, DescriptorType, Allocate(), Build() (+9 more)

### Community 12 - "Mesh Data Loading (OBJ)"
Cohesion: 0.11
Nodes (25): AttachmentConfig, AttachmentName, ImageData, AttachmentNameToString(), ConfigFor(), createAttachment(), HasAttachment(), RenderCache() (+17 more)

### Community 13 - "Descriptor Pool Management"
Cohesion: 0.10
Nodes (31): InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), resolveResourcePath(), Run(), StartRenderLoop(), WireSignals() (+23 more)

### Community 15 - "Editor Context & Scene State"
Cohesion: 0.19
Nodes (11): activeScene(), GetActiveCamera(), GetObjectID(), GetObjectIDs(), SetScene(), Camera, EditorContext(), ObjectID (+3 more)

### Community 16 - "Light Component System"
Cohesion: 0.15
Nodes (8): LightType, pair, Light(), ParseLightName(), SetColor(), SpriteType, string, vec3

### Community 17 - "Vulkan Context Initialization"
Cohesion: 0.18
Nodes (14): Instance, CreateInstance(), debugCallback(), findGraphicsQueueFamily(), getRequiredInstanceExtensions(), initDevice(), selectPhysicalDeviceIndex(), VulkanContext() (+6 more)

### Community 18 - "GPU Buffer Abstraction"
Cohesion: 0.22
Nodes (11): BufferUsageFlags, findMemoryType(), GetDescriptorInfo(), Upload(), VulkanBuffer(), DescriptorBufferInfo, Device, DeviceSize (+3 more)

### Community 19 - "Image Data Asset Loading"
Cohesion: 0.25
Nodes (15): ChannelCount(), ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), IsBGRFormat(), LoadFromPath(), PixelByteSize(), SavePixelData() (+7 more)

### Community 20 - "Attachment Manager"
Cohesion: 0.09
Nodes (26): array, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, CreateDescriptorSetLayout(), CreatePipeline(), DeterministicRNG (+18 more)

### Community 22 - "Camera System"
Cohesion: 0.21
Nodes (11): MatDataType, MaterialRes, MatParaType, InitParamData(), LoadMaterial(), Material(), ParseConfig(), SetMatParam() (+3 more)

### Community 23 - "Scene Graph Management"
Cohesion: 0.14
Nodes (12): Camera(), DebugLine(), DebugPoints(), Mesh(), ReleaseGPUBuffers(), ReloadMeshData(), Scene(), Sprite() (+4 more)

### Community 24 - "Editor Core State"
Cohesion: 0.08
Nodes (24): mat4, GenFloatData(), GetProjectionMatrix(), GetViewMatrix(), SetCamPos(), SetTarPos(), BuildIBLTextures(), CreateCubemapSampler() (+16 more)

### Community 25 - "Shader Module & Pipeline Stages"
Cohesion: 0.20
Nodes (6): CheckStatus(), GetActiveCamera(), GetObjectID(), SceneModifStatus, Camera, ObjectID

### Community 26 - "Scene Mesh GPU Upload"
Cohesion: 0.24
Nodes (8): _Base, neurus(), RenderContext(), hovered_object(), class, Context(), EditorContext(), namespace

### Community 27 - "Camera Controller (Orbit/Pan/Zoom)"
Cohesion: 0.31
Nodes (9): PipelineShaderStageCreateInfo, FromEmbedded(), FromFile(), GetStageInfo(), ShaderModule(), ShaderStageFlagBits, Device, string (+1 more)

### Community 28 - "Buffer Layout Attributes"
Cohesion: 0.48
Nodes (6): AddAttribute(), GetBindingDescription(), GetFormatSize(), GetStride(), Format, VertexInputBindingDescription

### Community 29 - "Project Save/Load"
Cohesion: 0.05
Nodes (39): IndexBuffer(), VertexBuffer(), Light, Mesh, CreateDefault(), New(), Open(), Save() (+31 more)

### Community 30 - "Command Buffer Wrapper"
Cohesion: 0.15
Nodes (23): DeferredRenderer, Edit(), Editor(), GenerateIBL(), Initialize(), OnCameraAdd(), OnIBLLoad(), OnLightAdd() (+15 more)

### Community 31 - "Debug Line Rendering"
Cohesion: 0.33
Nodes (5): neurus(), SetScissor(), SetViewport(), namespace, vk

### Community 33 - "Shader Library Cache"
Cohesion: 0.18
Nodes (20): QDoubleSpinBox, Camera, ObjectID, QString, QWidget, Scene, namespace, Transform3D (+12 more)

### Community 34 - "Index Buffer GPU Resource"
Cohesion: 0.47
Nodes (4): PushDebugLine(), PushDebugLines(), vec3, vector

### Community 35 - "Vertex Buffer GPU Resource"
Cohesion: 0.47
Nodes (4): PushDebugPoint(), PushDebugPoints(), vec3, vector

### Community 36 - "Compute Pipeline Builder"
Cohesion: 0.19
Nodes (19): createDepthmap(), CreateLightLayout(), createMultiviewColorPipeline(), createMultiviewPipeline(), createSingleFacePipeline(), createUniforms(), Record(), SetLightPosition() (+11 more)

### Community 39 - "Graphics Pipeline Builder"
Cohesion: 0.50
Nodes (4): BuildComputePipeline(), ComputePipelineBuilder(), Device, Pipeline

### Community 40 - "Sprite Component"
Cohesion: 0.67
Nodes (3): neurus(), raii(), namespace

### Community 41 - "Shader Program Pipeline"
Cohesion: 0.83
Nodes (3): gpuName(), setGpuName(), QString

### Community 42 - "Neurus Main Window"
Cohesion: 0.67
Nodes (3): BuildGraphicsPipeline(), Device, Pipeline

### Community 46 - "Core Parameters"
Cohesion: 0.67
Nodes (3): namespace, ads(), neurus()

### Community 54 - "Deferred Renderer Header"
Cohesion: 0.06
Nodes (44): ComputePipelineBuilder, DescriptorSetLayoutBuilder, CreateCameraLayout(), CreatePipeline(), GeometryPass(), Record(), CreateDescriptorSetLayout(), CreateEquirectSampler() (+36 more)

### Community 56 - "Geometry Pass Header"
Cohesion: 0.11
Nodes (13): CreateDescriptorSets(), DispatchCompute(), neurus(), neurus(), neurus(), neurus(), neurus(), namespace (+5 more)

### Community 59 - "Screenshot Header"
Cohesion: 0.30
Nodes (11): CameraPushEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush(), OnCameraRotate() (+3 more)

### Community 79 - "Editor Context Cpp"
Cohesion: 0.10
Nodes (22): neurus(), neurus(), CreateDescriptorSetLayout(), CreatePipeline(), GetLightSSBO(), LightingPass(), Record(), UploadLights() (+14 more)

### Community 81 - "Community 81"
Cohesion: 0.33
Nodes (5): neurus(), neurus(), project(), namespace, namespace

## Knowledge Gaps
- **231 isolated node(s):** `DescriptorSetLayout`, `DescriptorSetLayoutBuilder`, `DescriptorBindingFlags`, `DescriptorBufferInfo`, `DescriptorImageInfo` (+226 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **31 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `array` connect `Attachment Manager` to `Deferred Renderer Core`, `Deferred Renderer Header`, `Editor Context Cpp`?**
  _High betweenness centrality (0.081) - this node is a cross-community bridge._
- **Why does `Environment` connect `Command Buffer Wrapper` to `Editor Core State`, `Project Save/Load`, `Editor Context Cpp`?**
  _High betweenness centrality (0.074) - this node is a cross-community bridge._
- **Why does `Light` connect `Project Save/Load` to `Light Component System`, `Shader Library Cache`, `Editor Context Cpp`?**
  _High betweenness centrality (0.071) - this node is a cross-community bridge._
- **What connects `DescriptorSetLayout`, `DescriptorSetLayoutBuilder`, `DescriptorBindingFlags` to the rest of the system?**
  _231 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Deferred Renderer Core` be split into smaller, more focused modules?**
  _Cohesion score 0.06342494714587738 - nodes in this community are weakly interconnected._
- **Should `Application & Window Events` be split into smaller, more focused modules?**
  _Cohesion score 0.10333333333333333 - nodes in this community are weakly interconnected._
- **Should `Scene Debug Visualization` be split into smaller, more focused modules?**
  _Cohesion score 0.059743954480796585 - nodes in this community are weakly interconnected._