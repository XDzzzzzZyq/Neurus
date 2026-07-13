# Graph Report - D:\Projects\Neurus  (2026-07-13)

## Corpus Check
- 78 files · ~2,940 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 588 nodes · 1041 edges · 24 communities (23 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 13 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Render Pipeline & SSAO|Render Pipeline & SSAO]]
- [[_COMMUNITY_Editor & Context|Editor & Context]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_Input System|Input System]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline (alt)|Descriptor & Shadow Pipeline (alt)]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Vulkan Context|Vulkan Context]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_Node Graph|Node Graph]]
- [[_COMMUNITY_SyncObjects|SyncObjects]]
- [[_COMMUNITY_Project Management|Project Management]]
- [[_COMMUNITY_ScalarSlider|ScalarSlider]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 43 edges
2. `Image` - 35 edges
3. `Editor` - 34 edges
4. `ShaderStruct` - 30 edges
5. `Input` - 22 edges
6. `Camera` - 21 edges
7. `DeferredRenderer` - 19 edges
8. `Texture` - 18 edges
9. `Light` - 18 edges
10. `Application` - 17 edges

## Surprising Connections (you probably didn't know these)
- `ShadowIntensityPass()` --calls--> `DescriptorPool()`  [INFERRED]
  src/render/passes/ShadowIntensityPass.cpp → src/render/DescriptorManager.cpp
- `CreateSunDescriptorSetLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/ShadowIntensityPass.cpp → src/render/DescriptorManager.cpp
- `GetLastSwapchainImage()` --references--> `Image`  [EXTRACTED]
  src/render/DeferredRenderer.cpp → src/render/Barrier.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp
- `LightingPass()` --calls--> `createSampler()`  [INFERRED]
  src/render/passes/LightingPass.cpp → src/render/Texture.cpp

## Import Cycles
- None detected.

## Communities (24 total, 1 thin omitted)

### Community 0 - "Render Pipeline & SSAO"
Cohesion: 0.06
Nodes (45): ComputePipelineBuilder, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, ComputePass, GeometryPass (+37 more)

### Community 1 - "Editor & Context"
Cohesion: 0.09
Nodes (43): Application, InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), NewFrameSignals(), PanelSignals(), RecreateSignals() (+35 more)

### Community 2 - "Image & Barrier System"
Cohesion: 0.09
Nodes (47): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+39 more)

### Community 3 - "Main Window & UI"
Cohesion: 0.07
Nodes (26): TypeRegistration, GetUIContext(), Mesh, OnItemClicked(), Refresh(), UIPanel, MeshBindings, UIContext (+18 more)

### Community 4 - "Scene & Camera Controllers"
Cohesion: 0.09
Nodes (36): CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush() (+28 more)

### Community 5 - "Shader Struct Parsing"
Cohesion: 0.09
Nodes (28): Args, ParaType, ExtractIntFromLayout(), GetStd140Layout(), HasLayoutKeyword(), ParseShaderCode(), ParseShaderFile(), StripComments() (+20 more)

### Community 6 - "Shader Compilation"
Cohesion: 0.10
Nodes (25): RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule(), CreateModuleFromSpirv(), GetFragmentModule() (+17 more)

### Community 7 - "Descriptor & Shadow Pipeline"
Cohesion: 0.09
Nodes (21): AttachmentConfig, AttachmentName, AttachmentNameToString(), ConfigFor(), createAttachment(), GetEnvironmentGPU(), GetLightGPU(), GetMeshGPU() (+13 more)

### Community 8 - "Debug Primitives & Transform"
Cohesion: 0.07
Nodes (15): PointType, SetCamPos(), SetColor(), SetOpacity(), SetPointType(), GlmSerialization, ComputeModelMatrix(), GetTransformPtr() (+7 more)

### Community 9 - "Texture & Material"
Cohesion: 0.10
Nodes (23): MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData() (+15 more)

### Community 10 - "Editor & Property UI"
Cohesion: 0.11
Nodes (19): LightType, AddSectionHeader(), ClearFormLayout(), CreateReadOnlySpinBox(), PopulateCamera(), PopulateHeader(), PopulateLight(), PopulateTransform() (+11 more)

### Community 11 - "Buffer Hierarchy"
Cohesion: 0.11
Nodes (21): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), GPUBuffer (+13 more)

### Community 12 - "Input System"
Cohesion: 0.10
Nodes (13): Edit(), GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld() (+5 more)

### Community 13 - "Editor & Property UI (alt)"
Cohesion: 0.24
Nodes (16): addComboRow(), addSpinRow(), BuildAmbientOcclusionSection(), BuildLightingSection(), BuildPipelineSection(), BuildPostProcessingSection(), BuildShadowsSection(), ConnectAllSignals() (+8 more)

### Community 14 - "Descriptor & Shadow Pipeline (alt)"
Cohesion: 0.18
Nodes (14): ShadowDepthPass, createPipeline(), CreateSSBOLayout(), createSSBOResources(), createSunPipeline(), MakeFaceVPs(), ShadowDepthPass(), Allocate() (+6 more)

### Community 15 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 16 - "Vulkan Context"
Cohesion: 0.25
Nodes (9): CreateInstance(), findGraphicsQueueFamily(), findGraphicsQueueFamilyWithPresent(), getRequiredInstanceExtensions(), InitDevice(), InitQueue(), selectPhysicalDeviceIndex(), Editor() (+1 more)

### Community 17 - "Pass System"
Cohesion: 0.44
Nodes (9): ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues(), PassType (+1 more)

### Community 18 - "GPU Image Core"
Cohesion: 0.47
Nodes (9): AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate(), Swapchain() (+1 more)

### Community 19 - "Buffer Layout"
Cohesion: 0.60
Nodes (4): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout

### Community 20 - "Node Graph"
Cohesion: 0.60
Nodes (5): Graph, Connect(), Disconnect(), SocketInT, SocketOutT

### Community 21 - "SyncObjects"
Cohesion: 0.50
Nodes (4): SyncObjects, sync_fence(), sync_semaphore(), device

### Community 23 - "ScalarSlider"
Cohesion: 0.67
Nodes (3): ScalarSlider(), setValue(), ScalarSlider

## Knowledge Gaps
- **33 isolated node(s):** `QStringList`, `lightViewProj`, `bias`, `layerIndex`, `rng_state` (+28 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Editor` connect `Editor & Context` to `Main Window & UI`, `Scene & Camera Controllers`, `Buffer Hierarchy`, `Input System`, `Vulkan Context`?**
  _High betweenness centrality (0.212) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Descriptor & Shadow Pipeline` to `Render Pipeline & SSAO`, `Editor & Context`, `Image & Barrier System`, `Texture & Material`, `Buffer Hierarchy`, `Descriptor & Shadow Pipeline (alt)`?**
  _High betweenness centrality (0.189) - this node is a cross-community bridge._
- **Why does `GeometryPass` connect `Render Pipeline & SSAO` to `Editor & Context`, `Main Window & UI`, `Shader Compilation`, `Descriptor & Shadow Pipeline`?**
  _High betweenness centrality (0.160) - this node is a cross-community bridge._
- **What connects `QStringList`, `lightViewProj`, `bias` to the rest of the system?**
  _33 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Render Pipeline & SSAO` be split into smaller, more focused modules?**
  _Cohesion score 0.061495457721872815 - nodes in this community are weakly interconnected._
- **Should `Editor & Context` be split into smaller, more focused modules?**
  _Cohesion score 0.08653061224489796 - nodes in this community are weakly interconnected._
- **Should `Image & Barrier System` be split into smaller, more focused modules?**
  _Cohesion score 0.09219858156028368 - nodes in this community are weakly interconnected._