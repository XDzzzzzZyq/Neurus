# Graph Report - D:\Projects\Neurus  (2026-07-12)

## Corpus Check
- 76 files · ~2,800 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 560 nodes · 980 edges · 21 communities
- Extraction: 98% EXTRACTED · 2% INFERRED · 0% AMBIGUOUS · INFERRED: 15 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Application Entry|Application Entry]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Shader Compilation (alt)|Shader Compilation (alt)]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Input System|Input System]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Scene & Camera Controllers (alt)|Scene & Camera Controllers (alt)]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_Node Graph|Node Graph]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 47 edges
2. `Image` - 36 edges
3. `ShaderStruct` - 32 edges
4. `DeferredRenderer` - 26 edges
5. `Editor` - 25 edges
6. `Light` - 23 edges
7. `Input` - 22 edges
8. `Texture` - 20 edges
9. `ImageData` - 17 edges
10. `RenderShader` - 17 edges

## Surprising Connections (you probably didn't know these)
- `HandleResize()` --calls--> `EventQueue`  [INFERRED]
  src/editor/Editor.cpp → src/editor/controllers/CameraController.cpp
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/DeferredRenderer.cpp
- `FromImage()` --references--> `Image`  [EXTRACTED]
  src/render/Texture.cpp → src/render/DeferredRenderer.cpp
- `UploadEnvironment()` --calls--> `ImageData`  [INFERRED]
  src/render/UploadManager.cpp → src/render/Image.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (21 total, 0 thin omitted)

### Community 0 - "Shader Compilation"
Cohesion: 0.06
Nodes (47): ComputePipelineBuilder, ComputeShader, ComputePass, SSAOPass, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount (+39 more)

### Community 1 - "Swapchain"
Cohesion: 0.06
Nodes (36): AttachmentConfig, AttachmentName, IndexBuffer, VertexBuffer, MeshGPU, Mesh, AttachmentNameToString(), ConfigFor() (+28 more)

### Community 2 - "Application Entry"
Cohesion: 0.08
Nodes (40): InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), ResizeViewport(), Run(), WireSignals(), CreateInstance() (+32 more)

### Community 3 - "Debug Primitives & Transform"
Cohesion: 0.06
Nodes (17): DebugLine, DebugPoints, GlmSerialization, Transform, UID, PointType, RecomputeMatrices(), SetCamPos() (+9 more)

### Community 4 - "Shader Compilation (alt)"
Cohesion: 0.10
Nodes (31): ShaderLibrary, ShaderParser, RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule() (+23 more)

### Community 5 - "Image & Barrier System"
Cohesion: 0.12
Nodes (35): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+27 more)

### Community 6 - "Editor & Property UI"
Cohesion: 0.09
Nodes (21): TypeRegistration, ShaderLib, DefaultScene, OutlinerPanel, PropertyEditor, Light, LightType, Light() (+13 more)

### Community 7 - "Texture & Material"
Cohesion: 0.10
Nodes (23): Material, MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment() (+15 more)

### Community 8 - "Shader Struct Parsing"
Cohesion: 0.11
Nodes (20): Args, ParaType, ADD_TYPE(), DefFunc(), DefStruct(), GenerateShader(), IsEmpty(), ParseArgs() (+12 more)

### Community 9 - "Buffer Hierarchy"
Cohesion: 0.11
Nodes (22): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), Buffer (+14 more)

### Community 10 - "GPU Image Core"
Cohesion: 0.12
Nodes (22): SyncObjects, RenderConfig, Swapchain, DeferredRenderer, HandleResize(), sync_fence(), sync_semaphore(), DeferredRenderer() (+14 more)

### Community 11 - "Input System"
Cohesion: 0.11
Nodes (11): Input, GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld() (+3 more)

### Community 12 - "Main Window & UI"
Cohesion: 0.11
Nodes (9): NeurusMainWindow, CreateDocks(), CreateMenus(), getVulkanWidget(), LoadLayout(), makePlaceholder(), NeurusMainWindow(), RestoreDefaultLayout() (+1 more)

### Community 13 - "Scene & Camera Controllers"
Cohesion: 0.19
Nodes (17): Selections, ClearSelection(), Deselect(), GetActiveObject(), GetSelectedObjects(), IsSelected(), Select(), EditorEvents (+9 more)

### Community 14 - "Descriptor & Shadow Pipeline"
Cohesion: 0.18
Nodes (14): DescriptorManager, ShadowDepthPass, createPipeline(), CreateSSBOLayout(), createSSBOResources(), createSunPipeline(), MakeFaceVPs(), ShadowDepthPass() (+6 more)

### Community 15 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 16 - "Scene & Camera Controllers (alt)"
Cohesion: 0.28
Nodes (13): CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush() (+5 more)

### Community 17 - "Editor & Property UI (alt)"
Cohesion: 0.23
Nodes (9): Camera, activeScene(), GetActiveCamera(), GetObjectID(), GetObjectIDs(), SetScene(), ObjectID, Scene (+1 more)

### Community 18 - "Pass System"
Cohesion: 0.44
Nodes (9): Pass, ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues() (+1 more)

### Community 19 - "Buffer Layout"
Cohesion: 0.38
Nodes (5): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout, PipelineBuilder

### Community 20 - "Node Graph"
Cohesion: 0.60
Nodes (5): Connect(), Disconnect(), Graph, SocketInT, SocketOutT

## Knowledge Gaps
- **30 isolated node(s):** `CameraZoomEvent`, `CameraRotateEvent`, `CameraPushEvent`, `CameraSlideEvent`, `CameraResizeEvent` (+25 more)
  These have ≤1 connection - possible missing edges or undocumented components.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `DeferredRenderer` connect `GPU Image Core` to `Shader Compilation`, `Swapchain`, `Application Entry`, `Image & Barrier System`, `Editor & Property UI`, `Texture & Material`, `Buffer Hierarchy`, `Pass System`?**
  _High betweenness centrality (0.405) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Swapchain` to `Shader Compilation`, `Image & Barrier System`, `Editor & Property UI`, `Texture & Material`, `Buffer Hierarchy`, `GPU Image Core`, `Descriptor & Shadow Pipeline`?**
  _High betweenness centrality (0.227) - this node is a cross-community bridge._
- **Why does `Light` connect `Editor & Property UI` to `Shader Compilation`, `Swapchain`, `Debug Primitives & Transform`, `Buffer Hierarchy`, `GPU Image Core`, `Descriptor & Shadow Pipeline`?**
  _High betweenness centrality (0.175) - this node is a cross-community bridge._
- **What connects `CameraZoomEvent`, `CameraRotateEvent`, `CameraPushEvent` to the rest of the system?**
  _30 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Shader Compilation` be split into smaller, more focused modules?**
  _Cohesion score 0.06363636363636363 - nodes in this community are weakly interconnected._
- **Should `Swapchain` be split into smaller, more focused modules?**
  _Cohesion score 0.061952861952861954 - nodes in this community are weakly interconnected._
- **Should `Application Entry` be split into smaller, more focused modules?**
  _Cohesion score 0.08309178743961353 - nodes in this community are weakly interconnected._