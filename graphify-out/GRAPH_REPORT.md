# Graph Report - D:\Projects\Neurus  (2026-07-13)

## Corpus Check
- 76 files · ~2,885 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 577 nodes · 1013 edges · 24 communities (23 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 13 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Render Pipeline & SSAO|Render Pipeline & SSAO]]
- [[_COMMUNITY_Editor & Context|Editor & Context]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Input System|Input System]]
- [[_COMMUNITY_Scene & Camera Controllers (alt)|Scene & Camera Controllers (alt)]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline (alt)|Descriptor & Shadow Pipeline (alt)]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
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
3. `Editor` - 33 edges
4. `ShaderStruct` - 30 edges
5. `Input` - 22 edges
6. `DeferredRenderer` - 19 edges
7. `Texture` - 18 edges
8. `Light` - 18 edges
9. `Application` - 17 edges
10. `ImageData` - 17 edges

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
Nodes (42): Application, InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), NewFrameSignals(), PanelSignals(), RecreateSignals() (+34 more)

### Community 2 - "Image & Barrier System"
Cohesion: 0.09
Nodes (47): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+39 more)

### Community 3 - "Descriptor & Shadow Pipeline"
Cohesion: 0.06
Nodes (25): TypeRegistration, AttachmentConfig, AttachmentName, Mesh, AttachmentNameToString(), ConfigFor(), createAttachment(), GetEnvironmentGPU() (+17 more)

### Community 4 - "Scene & Camera Controllers"
Cohesion: 0.08
Nodes (34): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), CameraPushEvent (+26 more)

### Community 5 - "Debug Primitives & Transform"
Cohesion: 0.06
Nodes (16): PointType, RecomputeMatrices(), SetCamPos(), SetColor(), SetOpacity(), SetPointType(), GlmSerialization, ComputeModelMatrix() (+8 more)

### Community 6 - "Shader Struct Parsing"
Cohesion: 0.09
Nodes (28): Args, ParaType, ExtractIntFromLayout(), GetStd140Layout(), HasLayoutKeyword(), ParseShaderCode(), ParseShaderFile(), StripComments() (+20 more)

### Community 7 - "Shader Compilation"
Cohesion: 0.10
Nodes (25): RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule(), CreateModuleFromSpirv(), GetFragmentModule() (+17 more)

### Community 8 - "Main Window & UI"
Cohesion: 0.10
Nodes (22): GetUIContext(), OnItemClicked(), Refresh(), UIPanel, UIContext, NeurusMainWindow, OutlinerPanel, Outliner (+14 more)

### Community 9 - "Texture & Material"
Cohesion: 0.10
Nodes (23): MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData() (+15 more)

### Community 10 - "Editor & Property UI"
Cohesion: 0.11
Nodes (19): LightType, AddSectionHeader(), ClearFormLayout(), CreateReadOnlySpinBox(), PopulateCamera(), PopulateHeader(), PopulateLight(), PopulateTransform() (+11 more)

### Community 11 - "Input System"
Cohesion: 0.10
Nodes (13): Edit(), GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld() (+5 more)

### Community 12 - "Scene & Camera Controllers (alt)"
Cohesion: 0.24
Nodes (17): Camera, Selections, GetActiveObject(), GetSelectedObjects(), IsSelected(), SelectionController, BoxSelect(), ClearSelection() (+9 more)

### Community 13 - "Descriptor & Shadow Pipeline (alt)"
Cohesion: 0.18
Nodes (14): ShadowDepthPass, createPipeline(), CreateSSBOLayout(), createSSBOResources(), createSunPipeline(), MakeFaceVPs(), ShadowDepthPass(), Allocate() (+6 more)

### Community 14 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 15 - "Editor & Property UI (alt)"
Cohesion: 0.32
Nodes (12): addComboRow(), addSpinRow(), BuildAmbientOcclusionSection(), BuildLightingSection(), BuildPipelineSection(), BuildPostProcessingSection(), BuildShadowsSection(), ConnectAllSignals() (+4 more)

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
- **30 isolated node(s):** `lightViewProj`, `bias`, `layerIndex`, `rng_state`, `KernelSampleGpu` (+25 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Editor` connect `Editor & Context` to `Scene & Camera Controllers`, `Main Window & UI`, `Input System`, `Scene & Camera Controllers (alt)`, `Vulkan Context`?**
  _High betweenness centrality (0.210) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Descriptor & Shadow Pipeline` to `Render Pipeline & SSAO`, `Editor & Context`, `Image & Barrier System`, `Scene & Camera Controllers`, `Texture & Material`, `Descriptor & Shadow Pipeline (alt)`?**
  _High betweenness centrality (0.204) - this node is a cross-community bridge._
- **Why does `Light` connect `Editor & Property UI` to `Render Pipeline & SSAO`, `Descriptor & Shadow Pipeline`, `Scene & Camera Controllers`, `Debug Primitives & Transform`, `Main Window & UI`, `Descriptor & Shadow Pipeline (alt)`?**
  _High betweenness centrality (0.189) - this node is a cross-community bridge._
- **What connects `lightViewProj`, `bias`, `layerIndex` to the rest of the system?**
  _30 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Render Pipeline & SSAO` be split into smaller, more focused modules?**
  _Cohesion score 0.061495457721872815 - nodes in this community are weakly interconnected._
- **Should `Editor & Context` be split into smaller, more focused modules?**
  _Cohesion score 0.08673469387755102 - nodes in this community are weakly interconnected._
- **Should `Image & Barrier System` be split into smaller, more focused modules?**
  _Cohesion score 0.09219858156028368 - nodes in this community are weakly interconnected._