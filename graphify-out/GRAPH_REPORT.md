# Graph Report - D:\Projects\Neurus  (2026-07-15)

## Corpus Check
- 77 files · ~2,870 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 574 nodes · 998 edges · 27 communities (26 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 13 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline (alt)|Descriptor & Shadow Pipeline (alt)]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Editor & Context|Editor & Context]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Vulkan Context|Vulkan Context]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_Descriptor & Buffer Layout|Descriptor & Buffer Layout]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Input|Input]]
- [[_COMMUNITY_OutlinerRow|OutlinerRow]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_Node Graph|Node Graph]]
- [[_COMMUNITY_SyncObjects|SyncObjects]]
- [[_COMMUNITY_ScalarSlider|ScalarSlider]]
- [[_COMMUNITY_Debug Primitives & Light|Debug Primitives & Light]]
- [[_COMMUNITY_UIContext|UIContext]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 43 edges
2. `Image` - 34 edges
3. `Editor` - 33 edges
4. `ShaderStruct` - 30 edges
5. `Camera` - 19 edges
6. `DeferredRenderer` - 18 edges
7. `Texture` - 18 edges
8. `ShadowIntensityPass` - 17 edges
9. `Application` - 17 edges
10. `ImageData` - 17 edges

## Surprising Connections (you probably didn't know these)
- `GetRenderContext()` --references--> `RenderContext`  [EXTRACTED]
  src/editor/Editor.cpp → src/render/passes/GeometryPass.cpp
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/Barrier.cpp
- `CaptureAllAttachments()` --calls--> `AttachmentNameToString()`  [INFERRED]
  src/render/Screenshot.cpp → src/render/RenderCache.cpp
- `UploadEnvironment()` --calls--> `ImageData`  [INFERRED]
  src/render/UploadManager.cpp → src/render/Image.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (27 total, 1 thin omitted)

### Community 0 - "Descriptor & Shadow Pipeline"
Cohesion: 0.05
Nodes (55): UniformBuffer, ComputePipelineBuilder, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, ComputePass (+47 more)

### Community 1 - "Image & Barrier System"
Cohesion: 0.09
Nodes (47): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+39 more)

### Community 2 - "Descriptor & Shadow Pipeline (alt)"
Cohesion: 0.06
Nodes (27): TypeRegistration, AttachmentConfig, AttachmentName, Light, Mesh, AttachmentNameToString(), ConfigFor(), createAttachment() (+19 more)

### Community 3 - "Scene & Camera Controllers"
Cohesion: 0.09
Nodes (34): CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, CameraController, Init(), NotifyCameraChanged() (+26 more)

### Community 4 - "Shader Struct Parsing"
Cohesion: 0.09
Nodes (28): Args, ParaType, ExtractIntFromLayout(), GetStd140Layout(), HasLayoutKeyword(), ParseShaderCode(), ParseShaderFile(), StripComments() (+20 more)

### Community 5 - "Swapchain"
Cohesion: 0.10
Nodes (31): DeferredRenderer, Editor, ChangeObjectVisibility(), GenerateIBL(), GetRenderContext(), GetUIContext(), HandleResize(), Initialize() (+23 more)

### Community 6 - "Shader Compilation"
Cohesion: 0.10
Nodes (25): RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule(), CreateModuleFromSpirv(), GetFragmentModule() (+17 more)

### Community 7 - "Debug Primitives & Transform"
Cohesion: 0.07
Nodes (14): PointType, SetColor(), SetOpacity(), SetPointType(), GlmSerialization, ComputeModelMatrix(), GetTransformPtr(), SetPosition() (+6 more)

### Community 8 - "Texture & Material"
Cohesion: 0.10
Nodes (23): MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData() (+15 more)

### Community 9 - "Editor & Context"
Cohesion: 0.12
Nodes (22): Application, InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), NewFrameSignals(), PanelSignals(), RecreateSignals() (+14 more)

### Community 10 - "Main Window & UI"
Cohesion: 0.09
Nodes (15): GOType, Outliner, AddCategoryGroup(), EnsureRowPool(), Refresh(), TypeIconName(), UIPanel, Viewport (+7 more)

### Community 11 - "Buffer Hierarchy"
Cohesion: 0.11
Nodes (21): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), GPUBuffer (+13 more)

### Community 12 - "Editor & Property UI"
Cohesion: 0.27
Nodes (13): AddSectionHeader(), Clear(), ClearFormLayout(), CreateReadOnlySpinBox(), LoadObject(), PopulateCamera(), PopulateHeader(), PopulateLight() (+5 more)

### Community 13 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 14 - "Editor & Property UI (alt)"
Cohesion: 0.33
Nodes (13): ConfigEvents, addComboRow(), addSpinRow(), BuildAmbientOcclusionSection(), BuildLightingSection(), BuildPipelineSection(), BuildPostProcessingSection(), BuildShadowsSection() (+5 more)

### Community 15 - "Vulkan Context"
Cohesion: 0.29
Nodes (8): CreateInstance(), findGraphicsQueueFamily(), findGraphicsQueueFamilyWithPresent(), getRequiredInstanceExtensions(), InitDevice(), InitQueue(), selectPhysicalDeviceIndex(), VulkanContext

### Community 16 - "Pass System"
Cohesion: 0.44
Nodes (9): ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues(), PassType (+1 more)

### Community 17 - "Descriptor & Buffer Layout"
Cohesion: 0.33
Nodes (6): Allocate(), Build(), CalculatePoolSizes(), DescriptorSet(), DescriptorSetLayout(), DescriptorManager

### Community 18 - "GPU Image Core"
Cohesion: 0.47
Nodes (9): AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate(), Swapchain() (+1 more)

### Community 19 - "Input"
Cohesion: 0.29
Nodes (6): GetModifiers(), GetMouseButton(), KeyboardModifiers, Modifiers, MouseButton, Input

### Community 20 - "OutlinerRow"
Cohesion: 0.43
Nodes (6): OutlinerRow, LoadOutlinerStyle(), OutlinerRow(), SetObject(), SetVisibilities(), UpdateToggleIcons()

### Community 21 - "Buffer Layout"
Cohesion: 0.60
Nodes (4): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout

### Community 22 - "Node Graph"
Cohesion: 0.60
Nodes (5): Graph, Connect(), Disconnect(), SocketInT, SocketOutT

### Community 23 - "SyncObjects"
Cohesion: 0.50
Nodes (4): SyncObjects, sync_fence(), sync_semaphore(), device

### Community 24 - "ScalarSlider"
Cohesion: 0.67
Nodes (3): ScalarSlider(), setValue(), ScalarSlider

### Community 25 - "Debug Primitives & Light"
Cohesion: 0.50
Nodes (3): LightType, ParseLightName(), SpriteType

## Knowledge Gaps
- **39 isolated node(s):** `AttachmentConfig`, `lightViewProj`, `bias`, `layerIndex`, `GOType` (+34 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `GeometryPass` connect `Descriptor & Shadow Pipeline` to `Descriptor & Shadow Pipeline (alt)`, `Swapchain`, `Shader Compilation`?**
  _High betweenness centrality (0.193) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Descriptor & Shadow Pipeline (alt)` to `Descriptor & Shadow Pipeline`, `Image & Barrier System`, `Swapchain`, `Texture & Material`, `Buffer Hierarchy`?**
  _High betweenness centrality (0.188) - this node is a cross-community bridge._
- **Why does `RenderShader` connect `Shader Compilation` to `Descriptor & Shadow Pipeline`, `Shader Struct Parsing`?**
  _High betweenness centrality (0.156) - this node is a cross-community bridge._
- **What connects `AttachmentConfig`, `lightViewProj`, `bias` to the rest of the system?**
  _39 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Descriptor & Shadow Pipeline` be split into smaller, more focused modules?**
  _Cohesion score 0.054563492063492064 - nodes in this community are weakly interconnected._
- **Should `Image & Barrier System` be split into smaller, more focused modules?**
  _Cohesion score 0.09219858156028368 - nodes in this community are weakly interconnected._
- **Should `Descriptor & Shadow Pipeline (alt)` be split into smaller, more focused modules?**
  _Cohesion score 0.05893719806763285 - nodes in this community are weakly interconnected._