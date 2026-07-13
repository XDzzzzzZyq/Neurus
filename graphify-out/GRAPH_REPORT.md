# Graph Report - D:\Projects\Neurus  (2026-07-13)

## Corpus Check
- 79 files · ~2,945 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 589 nodes · 1040 edges · 24 communities (23 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 13 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Application Entry|Application Entry]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline (alt)|Descriptor & Shadow Pipeline (alt)]]
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_Input System|Input System]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Vulkan Context|Vulkan Context]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_Descriptor & Buffer Layout|Descriptor & Buffer Layout]]
- [[_COMMUNITY_GPU Image Core (alt)|GPU Image Core (alt)]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_Node Graph|Node Graph]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_SyncObjects|SyncObjects]]
- [[_COMMUNITY_Project Management|Project Management]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 43 edges
2. `Image` - 33 edges
3. `ShaderStruct` - 30 edges
4. `Editor` - 26 edges
5. `Scene` - 24 edges
6. `Input` - 22 edges
7. `DeferredRenderer` - 20 edges
8. `Camera` - 19 edges
9. `RenderConfigPanel` - 18 edges
10. `ImageData` - 18 edges

## Surprising Connections (you probably didn't know these)
- `GetLastSwapchainImage()` --references--> `Image`  [EXTRACTED]
  src/render/DeferredRenderer.cpp → src/render/Barrier.cpp
- `CreateDefaultScene()` --references--> `Scene`  [EXTRACTED]
  src/scene/DefaultScene.cpp → src/editor/Editor.cpp
- `GetActiveCamera()` --references--> `Camera`  [EXTRACTED]
  src/scene/Scene.cpp → src/editor/SelectionController.cpp
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/Barrier.cpp
- `FromImage()` --references--> `Image`  [EXTRACTED]
  src/render/Texture.cpp → src/render/Barrier.cpp

## Import Cycles
- None detected.

## Communities (24 total, 1 thin omitted)

### Community 0 - "Descriptor & Shadow Pipeline"
Cohesion: 0.06
Nodes (55): UniformBuffer, ComputePipelineBuilder, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, ComputePass (+47 more)

### Community 1 - "Application Entry"
Cohesion: 0.07
Nodes (39): Application, InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), ResizeViewport(), Run(), WireSignals() (+31 more)

### Community 2 - "GPU Image Core"
Cohesion: 0.11
Nodes (42): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+34 more)

### Community 3 - "Main Window & UI"
Cohesion: 0.07
Nodes (25): TypeRegistration, Mesh, OnItemClicked(), Refresh(), UIPanel, CreateDefaultScene(), MeshBindings, DefaultScene (+17 more)

### Community 4 - "Scene & Camera Controllers"
Cohesion: 0.09
Nodes (36): CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush() (+28 more)

### Community 5 - "Shader Struct Parsing"
Cohesion: 0.09
Nodes (28): Args, ParaType, ExtractIntFromLayout(), GetStd140Layout(), HasLayoutKeyword(), ParseShaderCode(), ParseShaderFile(), StripComments() (+20 more)

### Community 6 - "Descriptor & Shadow Pipeline (alt)"
Cohesion: 0.08
Nodes (22): AttachmentConfig, AttachmentName, AttachmentNameToString(), ConfigFor(), createAttachment(), GetEnvironmentGPU(), GetLightGPU(), GetMeshGPU() (+14 more)

### Community 7 - "Shader Compilation"
Cohesion: 0.10
Nodes (25): RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule(), CreateModuleFromSpirv(), GetFragmentModule() (+17 more)

### Community 8 - "Debug Primitives & Transform"
Cohesion: 0.07
Nodes (14): PointType, SetColor(), SetOpacity(), SetPointType(), GlmSerialization, ComputeModelMatrix(), GetTransformPtr(), SetPosition() (+6 more)

### Community 9 - "Texture & Material"
Cohesion: 0.10
Nodes (23): MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData() (+15 more)

### Community 10 - "Editor & Property UI"
Cohesion: 0.12
Nodes (20): LightType, AddSectionHeader(), ClearFormLayout(), CreateReadOnlySpinBox(), PopulateCamera(), PopulateHeader(), PopulateLight(), PopulateTransform() (+12 more)

### Community 11 - "Buffer Hierarchy"
Cohesion: 0.11
Nodes (21): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), GPUBuffer (+13 more)

### Community 12 - "Input System"
Cohesion: 0.10
Nodes (13): Edit(), GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld() (+5 more)

### Community 13 - "Editor & Property UI (alt)"
Cohesion: 0.22
Nodes (20): addComboRow(), addSliderSpinRow(), addSpinRow(), BuildAmbientOcclusionSection(), BuildLightingSection(), BuildPipelineSection(), BuildPostProcessingSection(), BuildShadowsSection() (+12 more)

### Community 14 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 15 - "Vulkan Context"
Cohesion: 0.25
Nodes (9): CreateInstance(), findGraphicsQueueFamily(), findGraphicsQueueFamilyWithPresent(), getRequiredInstanceExtensions(), InitDevice(), InitQueue(), selectPhysicalDeviceIndex(), Editor() (+1 more)

### Community 16 - "Pass System"
Cohesion: 0.44
Nodes (9): ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues(), PassType (+1 more)

### Community 17 - "Descriptor & Buffer Layout"
Cohesion: 0.33
Nodes (6): Allocate(), Build(), CalculatePoolSizes(), DescriptorSet(), DescriptorSetLayout(), DescriptorManager

### Community 18 - "GPU Image Core (alt)"
Cohesion: 0.47
Nodes (9): AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate(), Swapchain() (+1 more)

### Community 19 - "Buffer Layout"
Cohesion: 0.60
Nodes (4): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout

### Community 20 - "Node Graph"
Cohesion: 0.60
Nodes (5): Graph, Connect(), Disconnect(), SocketInT, SocketOutT

### Community 21 - "Image & Barrier System"
Cohesion: 0.60
Nodes (5): ImageState, ToVulkanImageState(), Transition(), Barrier, VulkanImageState

### Community 22 - "SyncObjects"
Cohesion: 0.50
Nodes (4): SyncObjects, sync_fence(), sync_semaphore(), device

## Knowledge Gaps
- **34 isolated node(s):** `Transform3D`, `QStringList`, `TypeRegistration`, `CameraZoomEvent`, `CameraRotateEvent` (+29 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `DeferredRenderer` connect `Application Entry` to `Descriptor & Shadow Pipeline`, `Descriptor & Shadow Pipeline (alt)`, `Texture & Material`, `Buffer Hierarchy`, `Editor & Property UI (alt)`, `Vulkan Context`?**
  _High betweenness centrality (0.240) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Descriptor & Shadow Pipeline (alt)` to `Descriptor & Shadow Pipeline`, `Application Entry`, `GPU Image Core`, `Texture & Material`, `Buffer Hierarchy`?**
  _High betweenness centrality (0.222) - this node is a cross-community bridge._
- **Why does `GeometryPass` connect `Descriptor & Shadow Pipeline` to `Main Window & UI`, `Descriptor & Shadow Pipeline (alt)`, `Shader Compilation`?**
  _High betweenness centrality (0.178) - this node is a cross-community bridge._
- **What connects `Transform3D`, `QStringList`, `TypeRegistration` to the rest of the system?**
  _34 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Descriptor & Shadow Pipeline` be split into smaller, more focused modules?**
  _Cohesion score 0.05505952380952381 - nodes in this community are weakly interconnected._
- **Should `Application Entry` be split into smaller, more focused modules?**
  _Cohesion score 0.07137254901960784 - nodes in this community are weakly interconnected._
- **Should `GPU Image Core` be split into smaller, more focused modules?**
  _Cohesion score 0.10741971207087486 - nodes in this community are weakly interconnected._