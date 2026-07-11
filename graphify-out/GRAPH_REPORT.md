# Graph Report - D:\Projects\Neurus  (2026-07-11)

## Corpus Check
- 78 files · ~2,850 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 570 nodes · 1021 edges · 19 communities
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 12 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Application Entry|Application Entry]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Shader Compilation (alt)|Shader Compilation (alt)]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_Input System|Input System]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_Node Graph|Node Graph]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 47 edges
2. `Image` - 36 edges
3. `Scene` - 36 edges
4. `ShaderStruct` - 32 edges
5. `DeferredRenderer` - 27 edges
6. `Light` - 24 edges
7. `Editor` - 24 edges
8. `Input` - 22 edges
9. `Camera` - 21 edges
10. `Texture` - 20 edges

## Surprising Connections (you probably didn't know these)
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/DeferredRenderer.cpp
- `FromImage()` --references--> `Image`  [EXTRACTED]
  src/render/Texture.cpp → src/render/DeferredRenderer.cpp
- `UploadEnvironment()` --calls--> `ImageData`  [INFERRED]
  src/render/UploadManager.cpp → src/render/Image.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp
- `ShadowIntensityPass()` --calls--> `DescriptorPool()`  [INFERRED]
  src/render/passes/ShadowIntensityPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (19 total, 0 thin omitted)

### Community 0 - "Shader Compilation"
Cohesion: 0.06
Nodes (47): ComputePipelineBuilder, ComputeShader, ComputePass, SSAOPass, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount (+39 more)

### Community 1 - "Editor & Property UI"
Cohesion: 0.06
Nodes (36): _Base, Context, SelectionManager, TypeRegistration, ShaderLib, DefaultScene, OutlinerPanel, PropertyEditor (+28 more)

### Community 2 - "Swapchain"
Cohesion: 0.07
Nodes (35): AttachmentConfig, AttachmentName, IndexBuffer, VertexBuffer, MeshGPU, AttachmentNameToString(), ConfigFor(), createAttachment() (+27 more)

### Community 3 - "Application Entry"
Cohesion: 0.09
Nodes (37): Application, InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), ResizeViewport(), Run(), StartRenderLoop() (+29 more)

### Community 4 - "Scene & Camera Controllers"
Cohesion: 0.08
Nodes (29): CameraPushEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush(), OnCameraRotate() (+21 more)

### Community 5 - "Shader Compilation (alt)"
Cohesion: 0.10
Nodes (31): ShaderLibrary, ShaderParser, RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule() (+23 more)

### Community 6 - "Debug Primitives & Transform"
Cohesion: 0.07
Nodes (15): DebugLine, DebugPoints, GlmSerialization, Transform, UID, PointType, SetOpacity(), SetPointType() (+7 more)

### Community 7 - "Image & Barrier System"
Cohesion: 0.12
Nodes (35): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+27 more)

### Community 8 - "Texture & Material"
Cohesion: 0.10
Nodes (23): Material, MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment() (+15 more)

### Community 9 - "Shader Struct Parsing"
Cohesion: 0.11
Nodes (20): Args, ParaType, ADD_TYPE(), DefFunc(), DefStruct(), GenerateShader(), IsEmpty(), ParseArgs() (+12 more)

### Community 10 - "Buffer Hierarchy"
Cohesion: 0.11
Nodes (22): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), Buffer (+14 more)

### Community 11 - "Input System"
Cohesion: 0.10
Nodes (13): Input, Edit(), GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased() (+5 more)

### Community 12 - "GPU Image Core"
Cohesion: 0.12
Nodes (22): SyncObjects, RenderConfig, Swapchain, DeferredRenderer, sync_fence(), sync_semaphore(), DeferredRenderer(), DrawFrame() (+14 more)

### Community 13 - "Main Window & UI"
Cohesion: 0.11
Nodes (9): NeurusMainWindow, CreateDocks(), CreateMenus(), getVulkanWidget(), LoadLayout(), makePlaceholder(), NeurusMainWindow(), RestoreDefaultLayout() (+1 more)

### Community 14 - "Descriptor & Shadow Pipeline"
Cohesion: 0.14
Nodes (15): DescriptorManager, Mesh, ShadowDepthPass, createPipeline(), CreateSSBOLayout(), createSSBOResources(), createSunPipeline(), MakeFaceVPs() (+7 more)

### Community 15 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 16 - "Pass System"
Cohesion: 0.44
Nodes (9): Pass, ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues() (+1 more)

### Community 17 - "Buffer Layout"
Cohesion: 0.38
Nodes (5): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout, PipelineBuilder

### Community 18 - "Node Graph"
Cohesion: 0.60
Nodes (5): Connect(), Disconnect(), Graph, SocketInT, SocketOutT

## Knowledge Gaps
- **32 isolated node(s):** `VulkanImageState`, `AttachmentConfig`, `mipLevel`, `maxStep`, `roughnessSq` (+27 more)
  These have ≤1 connection - possible missing edges or undocumented components.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Scene` connect `Editor & Property UI` to `Shader Compilation`, `Swapchain`, `Application Entry`, `Scene & Camera Controllers`, `Debug Primitives & Transform`, `GPU Image Core`, `Main Window & UI`, `Descriptor & Shadow Pipeline`?**
  _High betweenness centrality (0.322) - this node is a cross-community bridge._
- **Why does `DeferredRenderer` connect `GPU Image Core` to `Shader Compilation`, `Editor & Property UI`, `Swapchain`, `Application Entry`, `Image & Barrier System`, `Texture & Material`, `Buffer Hierarchy`, `Pass System`?**
  _High betweenness centrality (0.292) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Swapchain` to `Shader Compilation`, `Editor & Property UI`, `Image & Barrier System`, `Texture & Material`, `Buffer Hierarchy`, `GPU Image Core`, `Descriptor & Shadow Pipeline`?**
  _High betweenness centrality (0.178) - this node is a cross-community bridge._
- **What connects `VulkanImageState`, `AttachmentConfig`, `mipLevel` to the rest of the system?**
  _32 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Shader Compilation` be split into smaller, more focused modules?**
  _Cohesion score 0.06363636363636363 - nodes in this community are weakly interconnected._
- **Should `Editor & Property UI` be split into smaller, more focused modules?**
  _Cohesion score 0.05870020964360587 - nodes in this community are weakly interconnected._
- **Should `Swapchain` be split into smaller, more focused modules?**
  _Cohesion score 0.06823529411764706 - nodes in this community are weakly interconnected._