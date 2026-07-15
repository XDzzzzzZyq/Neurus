# Graph Report - D:\Projects\Neurus  (2026-07-15)

## Corpus Check
- 78 files · ~2,895 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 579 nodes · 1011 edges · 25 communities (24 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 13 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Render Pipeline & SSAO|Render Pipeline & SSAO]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Editor & Context|Editor & Context]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Vulkan Context|Vulkan Context]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_Descriptor & Buffer Layout|Descriptor & Buffer Layout]]
- [[_COMMUNITY_GPU Image Core (alt)|GPU Image Core (alt)]]
- [[_COMMUNITY_Input|Input]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_Node Graph|Node Graph]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_OutlinerRow|OutlinerRow]]
- [[_COMMUNITY_SyncObjects|SyncObjects]]
- [[_COMMUNITY_ScalarSlider|ScalarSlider]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 43 edges
2. `Editor` - 35 edges
3. `Image` - 33 edges
4. `ShaderStruct` - 30 edges
5. `Camera` - 20 edges
6. `Application` - 18 edges
7. `DeferredRenderer` - 18 edges
8. `Texture` - 18 edges
9. `ImageData` - 17 edges
10. `PropertyEditor` - 16 edges

## Surprising Connections (you probably didn't know these)
- `GetLastSwapchainImage()` --references--> `Image`  [EXTRACTED]
  src/render/DeferredRenderer.cpp → src/render/Barrier.cpp
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/Barrier.cpp
- `FromImage()` --references--> `Image`  [EXTRACTED]
  src/render/Texture.cpp → src/render/Barrier.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp
- `CreateSSBOLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/ShadowDepthPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (25 total, 1 thin omitted)

### Community 0 - "Render Pipeline & SSAO"
Cohesion: 0.06
Nodes (46): UniformBuffer, ComputePipelineBuilder, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, ComputePass (+38 more)

### Community 1 - "Descriptor & Shadow Pipeline"
Cohesion: 0.06
Nodes (33): TypeRegistration, AttachmentConfig, AttachmentName, Mesh, createPipeline(), CreateSSBOLayout(), createSSBOResources(), createSunPipeline() (+25 more)

### Community 2 - "Editor & Property UI"
Cohesion: 0.07
Nodes (34): GetUIContext(), ConfigEvents, GOType, ObjectID, Outliner, AddCategoryGroup(), EnsureRowPool(), Refresh() (+26 more)

### Community 3 - "GPU Image Core"
Cohesion: 0.11
Nodes (42): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+34 more)

### Community 4 - "Scene & Camera Controllers"
Cohesion: 0.09
Nodes (33): CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, CameraController, Init(), NotifyCameraChanged() (+25 more)

### Community 5 - "Shader Struct Parsing"
Cohesion: 0.09
Nodes (28): Args, ParaType, ExtractIntFromLayout(), GetStd140Layout(), HasLayoutKeyword(), ParseShaderCode(), ParseShaderFile(), StripComments() (+20 more)

### Community 6 - "Shader Compilation"
Cohesion: 0.10
Nodes (25): RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule(), CreateModuleFromSpirv(), GetFragmentModule() (+17 more)

### Community 7 - "Debug Primitives & Transform"
Cohesion: 0.07
Nodes (15): PointType, SetCamPos(), SetColor(), SetOpacity(), SetPointType(), GlmSerialization, ComputeModelMatrix(), GetTransformPtr() (+7 more)

### Community 8 - "Swapchain"
Cohesion: 0.12
Nodes (27): DeferredRenderer, ChangeObjectVisibility(), GenerateIBL(), GetRenderContext(), HandleResize(), Initialize(), OnCameraAdd(), OnIBLLoad() (+19 more)

### Community 9 - "Texture & Material"
Cohesion: 0.10
Nodes (23): MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData() (+15 more)

### Community 10 - "Editor & Context"
Cohesion: 0.12
Nodes (23): InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), NewFrameSignals(), PanelSignals(), RecreateSignals(), ResizeViewport() (+15 more)

### Community 11 - "Editor & Property UI (alt)"
Cohesion: 0.11
Nodes (19): LightType, AddSectionHeader(), Clear(), ClearFormLayout(), CreateReadOnlySpinBox(), LoadObject(), PopulateCamera(), PopulateHeader() (+11 more)

### Community 12 - "Buffer Hierarchy"
Cohesion: 0.11
Nodes (21): createBuffer(), findMemoryType(), GPUBuffer(), Map(), StagingBuffer(), Unmap(), Upload(), GPUBuffer (+13 more)

### Community 13 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 14 - "Vulkan Context"
Cohesion: 0.29
Nodes (8): CreateInstance(), findGraphicsQueueFamily(), findGraphicsQueueFamilyWithPresent(), getRequiredInstanceExtensions(), InitDevice(), InitQueue(), selectPhysicalDeviceIndex(), VulkanContext

### Community 15 - "Pass System"
Cohesion: 0.44
Nodes (9): ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues(), PassType (+1 more)

### Community 16 - "Descriptor & Buffer Layout"
Cohesion: 0.33
Nodes (6): Allocate(), Build(), CalculatePoolSizes(), DescriptorSet(), DescriptorSetLayout(), DescriptorManager

### Community 17 - "GPU Image Core (alt)"
Cohesion: 0.47
Nodes (9): AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate(), Swapchain() (+1 more)

### Community 18 - "Input"
Cohesion: 0.25
Nodes (8): GetModifiers(), GetMouseButton(), GetMousePos(), KeyboardModifiers, Modifiers, MouseButton, QPointF, Input

### Community 19 - "Buffer Layout"
Cohesion: 0.60
Nodes (4): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout

### Community 20 - "Node Graph"
Cohesion: 0.60
Nodes (5): Graph, Connect(), Disconnect(), SocketInT, SocketOutT

### Community 21 - "Image & Barrier System"
Cohesion: 0.60
Nodes (5): ImageState, ToVulkanImageState(), Transition(), Barrier, VulkanImageState

### Community 23 - "SyncObjects"
Cohesion: 0.50
Nodes (4): SyncObjects, sync_fence(), sync_semaphore(), device

### Community 24 - "ScalarSlider"
Cohesion: 0.67
Nodes (3): ScalarSlider(), setValue(), ScalarSlider

## Knowledge Gaps
- **39 isolated node(s):** `ConfigEvents`, `EditorEvents`, `emit`, `ProjectOpenEvent`, `QPointF` (+34 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Editor` connect `Swapchain` to `Editor & Property UI`, `Scene & Camera Controllers`, `Editor & Context`, `Buffer Hierarchy`, `Vulkan Context`?**
  _High betweenness centrality (0.256) - this node is a cross-community bridge._
- **Why does `RenderCache` connect `Descriptor & Shadow Pipeline` to `Render Pipeline & SSAO`, `GPU Image Core`, `Swapchain`, `Texture & Material`, `Buffer Hierarchy`?**
  _High betweenness centrality (0.187) - this node is a cross-community bridge._
- **Why does `GeometryPass` connect `Render Pipeline & SSAO` to `Swapchain`, `Descriptor & Shadow Pipeline`, `Shader Compilation`?**
  _High betweenness centrality (0.168) - this node is a cross-community bridge._
- **What connects `ConfigEvents`, `EditorEvents`, `emit` to the rest of the system?**
  _39 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Render Pipeline & SSAO` be split into smaller, more focused modules?**
  _Cohesion score 0.06060606060606061 - nodes in this community are weakly interconnected._
- **Should `Descriptor & Shadow Pipeline` be split into smaller, more focused modules?**
  _Cohesion score 0.05656108597285068 - nodes in this community are weakly interconnected._
- **Should `Editor & Property UI` be split into smaller, more focused modules?**
  _Cohesion score 0.0663265306122449 - nodes in this community are weakly interconnected._