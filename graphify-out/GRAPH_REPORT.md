# Graph Report - D:\Projects\Neurus  (2026-07-12)

## Corpus Check
- 75 files · ~2,735 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 547 nodes · 926 edges · 20 communities (19 shown, 1 thin omitted)
- Extraction: 98% EXTRACTED · 2% INFERRED · 0% AMBIGUOUS · INFERRED: 15 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Application Entry|Application Entry]]
- [[_COMMUNITY_Shader Compilation (alt)|Shader Compilation (alt)]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_Input System|Input System]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Descriptor & Buffer Layout|Descriptor & Buffer Layout]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Scene & Camera Controllers (alt)|Scene & Camera Controllers (alt)]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_Node Graph|Node Graph]]
- [[_COMMUNITY_Project Management|Project Management]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 46 edges
2. `ShaderStruct` - 32 edges
3. `Editor` - 26 edges
4. `DeferredRenderer` - 24 edges
5. `Light` - 23 edges
6. `Input` - 22 edges
7. `Texture` - 19 edges
8. `RenderShader` - 17 edges
9. `ShadowIntensityPass` - 16 edges
10. `ImageData` - 16 edges

## Surprising Connections (you probably didn't know these)
- `CreateSunDescriptorSetLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/ShadowIntensityPass.cpp → src/render/DescriptorManager.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp
- `UploadEnvironment()` --calls--> `ImageData`  [INFERRED]
  src/render/UploadManager.cpp → src/render/Image.cpp
- `ExportShadowDepthEquirect()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/Screenshot.cpp → src/render/DescriptorManager.cpp
- `Init()` --references--> `EventQueue`  [EXTRACTED]
  src/editor/controllers/CameraController.cpp → src/editor/Editor.cpp

## Import Cycles
- None detected.

## Communities (20 total, 1 thin omitted)

### Community 0 - "Shader Compilation"
Cohesion: 0.06
Nodes (54): ComputePipelineBuilder, ComputeShader, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, GeometryPass (+46 more)

### Community 1 - "Swapchain"
Cohesion: 0.06
Nodes (35): AttachmentConfig, AttachmentName, IndexBuffer, VertexBuffer, MeshGPU, Mesh, AttachmentNameToString(), ConfigFor() (+27 more)

### Community 2 - "Debug Primitives & Transform"
Cohesion: 0.06
Nodes (17): DebugLine, DebugPoints, GlmSerialization, Transform, UID, PointType, RecomputeMatrices(), SetCamPos() (+9 more)

### Community 3 - "Application Entry"
Cohesion: 0.11
Nodes (33): InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), ResizeViewport(), Run(), WireSignals(), CreateInstance() (+25 more)

### Community 4 - "Shader Compilation (alt)"
Cohesion: 0.10
Nodes (31): ShaderLibrary, ShaderParser, RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule() (+23 more)

### Community 5 - "Editor & Property UI"
Cohesion: 0.09
Nodes (21): TypeRegistration, ShaderLib, DefaultScene, Light, LightType, Light(), ParseLightName(), SpriteType (+13 more)

### Community 6 - "Image & Barrier System"
Cohesion: 0.09
Nodes (28): SyncObjects, RenderConfig, Swapchain, DeferredRenderer, HandleResize(), Image, ImageState, sync_fence() (+20 more)

### Community 7 - "Texture & Material"
Cohesion: 0.10
Nodes (22): Material, MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment() (+14 more)

### Community 8 - "Shader Struct Parsing"
Cohesion: 0.11
Nodes (20): Args, ParaType, ADD_TYPE(), DefFunc(), DefStruct(), GenerateShader(), IsEmpty(), ParseArgs() (+12 more)

### Community 9 - "GPU Image Core"
Cohesion: 0.10
Nodes (21): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+13 more)

### Community 10 - "Buffer Hierarchy"
Cohesion: 0.11
Nodes (22): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), Buffer (+14 more)

### Community 11 - "Input System"
Cohesion: 0.10
Nodes (13): Input, Edit(), GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased() (+5 more)

### Community 12 - "Main Window & UI"
Cohesion: 0.11
Nodes (9): NeurusMainWindow, CreateDocks(), CreateMenus(), getViewport(), LoadLayout(), makePlaceholder(), NeurusMainWindow(), RestoreDefaultLayout() (+1 more)

### Community 13 - "Descriptor & Buffer Layout"
Cohesion: 0.15
Nodes (16): DescriptorManager, ComputePass, Pass, ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor() (+8 more)

### Community 14 - "Scene & Camera Controllers"
Cohesion: 0.20
Nodes (19): Selections, GetActiveObject(), GetSelectedObjects(), IsSelected(), EditorEvents, Editor(), BoxSelect(), ClearSelection() (+11 more)

### Community 15 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 16 - "Scene & Camera Controllers (alt)"
Cohesion: 0.28
Nodes (13): CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush() (+5 more)

### Community 17 - "Buffer Layout"
Cohesion: 0.38
Nodes (5): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout, PipelineBuilder

### Community 18 - "Node Graph"
Cohesion: 0.60
Nodes (5): Connect(), Disconnect(), Graph, SocketInT, SocketOutT

## Knowledge Gaps
- **32 isolated node(s):** `lightViewProj`, `bias`, `layerIndex`, `VulkanImageState`, `rng_state` (+27 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `DeferredRenderer` connect `Image & Barrier System` to `Shader Compilation`, `Swapchain`, `Application Entry`, `Editor & Property UI`, `Texture & Material`, `Buffer Hierarchy`, `Descriptor & Buffer Layout`, `Scene & Camera Controllers`?**
  _High betweenness centrality (0.372) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Swapchain` to `Shader Compilation`, `Editor & Property UI`, `Image & Barrier System`, `Texture & Material`, `GPU Image Core`, `Buffer Hierarchy`?**
  _High betweenness centrality (0.346) - this node is a cross-community bridge._
- **Why does `Light` connect `Editor & Property UI` to `Shader Compilation`, `Swapchain`, `Debug Primitives & Transform`, `Image & Barrier System`, `Buffer Hierarchy`?**
  _High betweenness centrality (0.207) - this node is a cross-community bridge._
- **What connects `lightViewProj`, `bias`, `layerIndex` to the rest of the system?**
  _32 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Shader Compilation` be split into smaller, more focused modules?**
  _Cohesion score 0.060655737704918035 - nodes in this community are weakly interconnected._
- **Should `Swapchain` be split into smaller, more focused modules?**
  _Cohesion score 0.061952861952861954 - nodes in this community are weakly interconnected._
- **Should `Debug Primitives & Transform` be split into smaller, more focused modules?**
  _Cohesion score 0.056910569105691054 - nodes in this community are weakly interconnected._