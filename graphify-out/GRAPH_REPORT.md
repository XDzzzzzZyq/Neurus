# Graph Report - D:\Projects\Neurus  (2026-08-02)

## Corpus Check
- 99 files · ~3,825 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 765 nodes · 1319 edges · 33 communities
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 18 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Transform Math|Transform Math]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Buffer Hierarchy|Buffer Hierarchy]]
- [[_COMMUNITY_Editor & Context|Editor & Context]]
- [[_COMMUNITY_Shader Struct Parsing (alt)|Shader Struct Parsing (alt)]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_CodeEditor|CodeEditor]]
- [[_COMMUNITY_Shader Compilation (alt)|Shader Compilation (alt)]]
- [[_COMMUNITY_Debug Primitives & Light|Debug Primitives & Light]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Shadow Eval Constants|Shadow Eval Constants]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_OutlinerRow|OutlinerRow]]
- [[_COMMUNITY_CodeEditor (alt)|CodeEditor (alt)]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Vulkan Context|Vulkan Context]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_Descriptor & Buffer Layout|Descriptor & Buffer Layout]]
- [[_COMMUNITY_GPU Image Core (alt)|GPU Image Core (alt)]]
- [[_COMMUNITY_MacOSSurface|MacOSSurface]]
- [[_COMMUNITY_Input|Input]]
- [[_COMMUNITY_ScalarSlider|ScalarSlider]]
- [[_COMMUNITY_Shader Compilation (alt)|Shader Compilation (alt)]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_SyncObjects|SyncObjects]]
- [[_COMMUNITY_Debug Primitives & Light (alt)|Debug Primitives & Light (alt)]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 48 edges
2. `Image` - 34 edges
3. `Init()` - 31 edges
4. `ShaderStruct` - 30 edges
5. `Editor` - 28 edges
6. `SceneController` - 22 edges
7. `Application` - 22 edges
8. `EventQueue` - 21 edges
9. `BuildPipeline()` - 20 edges
10. `DeferredRenderer` - 17 edges

## Surprising Connections (you probably didn't know these)
- `InitVulkan()` --calls--> `CreatePlatformSurface()`  [INFERRED]
  src/app/Application.cpp → src/platform/PlatformSurface.cpp
- `GetLastSwapchainImage()` --references--> `Image`  [EXTRACTED]
  src/render/DeferredRenderer.cpp → src/render/Barrier.cpp
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/Barrier.cpp
- `FromImage()` --references--> `Image`  [EXTRACTED]
  src/render/Texture.cpp → src/render/Barrier.cpp
- `CreateLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/FXAAPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (33 total, 0 thin omitted)

### Community 0 - "Shader Compilation"
Cohesion: 0.05
Nodes (62): UniformBuffer, ComputeShader, DescriptorSetLayoutBuilder, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, ComposePass() (+54 more)

### Community 1 - "Shader Struct Parsing"
Cohesion: 0.07
Nodes (64): CameraFovChanged, CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraTargetChanged, CameraZoomEvent, NotifyCameraChanged() (+56 more)

### Community 2 - "Descriptor & Shadow Pipeline"
Cohesion: 0.06
Nodes (38): AttachmentConfig, Graph, Disconnect(), Editor(), NodeT, AddPass(), Clear(), Connect() (+30 more)

### Community 3 - "GPU Image Core"
Cohesion: 0.10
Nodes (43): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+35 more)

### Community 4 - "Editor & Property UI"
Cohesion: 0.08
Nodes (35): AddCategoryGroup(), EnsureRowPool(), Outliner(), BuildHeader(), BuildTransformEditor(), BuildTypeSubpanels(), PropertyPanel(), Refresh() (+27 more)

### Community 5 - "Transform Math"
Cohesion: 0.06
Nodes (22): Camera(), ChangeCamPersp(), ChangeCamRatio(), RecomputeMatrices(), SetPosition(), SetTarPos(), CreateDefaultScene(), GlmSerialization (+14 more)

### Community 6 - "Swapchain"
Cohesion: 0.09
Nodes (32): Controllers, DeferredRenderer, Editor, FinishLoad(), GenerateIBL(), GetRenderContext(), GetUIContext(), Initialize() (+24 more)

### Community 7 - "Buffer Hierarchy"
Cohesion: 0.08
Nodes (29): Buffer, ArrayBuffer, createBuffer(), findMemoryType(), GPUBuffer(), Map(), Unmap(), Upload() (+21 more)

### Community 8 - "Editor & Context"
Cohesion: 0.13
Nodes (21): BuildProject(), InitEditor(), InitRenderer(), InitVulkan(), NewFrameSignals(), OnProjectNew(), OnProjectOpen(), OnProjectSave() (+13 more)

### Community 9 - "Shader Struct Parsing (alt)"
Cohesion: 0.11
Nodes (18): Args, ParaType, ADD_TYPE(), DefFunc(), DefStruct(), ParseArgs(), ParseType(), SetAB() (+10 more)

### Community 10 - "Texture & Material"
Cohesion: 0.11
Nodes (22): MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData() (+14 more)

### Community 11 - "CodeEditor"
Cohesion: 0.11
Nodes (17): CodeEditor(), lineNumberAreaWidth(), paintEvent(), resizeEvent(), setLineNumbersVisible(), updateLineNumberAreaWidth(), applyCpp(), applyGlsl() (+9 more)

### Community 12 - "Shader Compilation (alt)"
Cohesion: 0.12
Nodes (24): RenderShader, S_Const, ParseAndGenerate(), Recompile(), HasStage(), TypeToString(), Compile(), CompileAll() (+16 more)

### Community 13 - "Debug Primitives & Light"
Cohesion: 0.09
Nodes (11): setObjectId(), CosineToDeg(), DegToCosine(), LightProperties(), setCutoff(), setOuterCutoff(), setShadowEnabled(), CameraProperties (+3 more)

### Community 14 - "Editor & Property UI (alt)"
Cohesion: 0.16
Nodes (22): ConfigComponent(), Load(), project(), SceneComponent(), Serializable, addComboRow(), addSpinRow(), BuildAmbientOcclusionSection() (+14 more)

### Community 15 - "Editor & Property UI (alt)"
Cohesion: 0.13
Nodes (15): ItemFlags, addSection(), buildTree(), data(), flags(), headerData(), nodeString(), sectionTitle() (+7 more)

### Community 16 - "Debug Primitives & Transform"
Cohesion: 0.11
Nodes (6): PointType, SetColor(), SetOpacity(), SetPointType(), DebugLine, DebugPoints

### Community 17 - "Shadow Eval Constants"
Cohesion: 0.20
Nodes (15): ShadowEvalPushConstants, alpha, bias, farPlane, frameCount, jitterX, jitterY, jitterZ (+7 more)

### Community 18 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 19 - "OutlinerRow"
Cohesion: 0.20
Nodes (12): LoadOutlinerStyle(), OutlinerRow(), setEyeBtnColor(), setObject(), setRenderBtnColor(), setSelectionMode(), setVisibilities(), SelectionMode (+4 more)

### Community 20 - "CodeEditor (alt)"
Cohesion: 0.21
Nodes (9): setReadOnly(), createEditor(), populateTypeCombo(), setModelData(), updateEditorGeometry(), ShaderFieldRow(), QStyleOptionViewItem, ShaderFieldDelegate (+1 more)

### Community 21 - "Image & Barrier System"
Cohesion: 0.24
Nodes (12): AccessFlags2, BufferState, ImageState, PipelineStageFlags2, ToVulkanBufferState(), ToVulkanImageState(), Transition(), VulkanBufferState (+4 more)

### Community 22 - "Vulkan Context"
Cohesion: 0.24
Nodes (10): CreateInstance(), findGraphicsQueueFamily(), findGraphicsQueueFamilyWithPresent(), findTransferQueueFamily(), InitDevice(), InitQueue(), selectPhysicalDeviceIndex(), CreatePlatformSurface() (+2 more)

### Community 23 - "Pass System"
Cohesion: 0.44
Nodes (9): Pass, ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues() (+1 more)

### Community 24 - "Descriptor & Buffer Layout"
Cohesion: 0.33
Nodes (6): Allocate(), Build(), CalculatePoolSizes(), DescriptorSet(), DescriptorSetLayout(), DescriptorManager

### Community 25 - "GPU Image Core (alt)"
Cohesion: 0.47
Nodes (9): AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate(), Swapchain() (+1 more)

### Community 26 - "MacOSSurface"
Cohesion: 0.33
Nodes (6): CAMetalLayer, NeurusMetalView, -initWithFrame, -isOpaque, -layerClass, NSView

### Community 27 - "Input"
Cohesion: 0.33
Nodes (5): GetModifiers(), GetMouseButton(), Modifiers, MouseButton, Input

### Community 28 - "ScalarSlider"
Cohesion: 0.40
Nodes (4): ScalarSlider(), setValue(), ScalarSlider, Vec3Spin

### Community 29 - "Shader Compilation (alt)"
Cohesion: 0.40
Nodes (3): SetOptimizationLevel(), ShaderCompiler(), ShaderCompiler

### Community 30 - "Buffer Layout"
Cohesion: 0.60
Nodes (4): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout

### Community 31 - "SyncObjects"
Cohesion: 0.50
Nodes (4): SyncObjects, sync_fence(), sync_semaphore(), device

### Community 32 - "Debug Primitives & Light (alt)"
Cohesion: 0.50
Nodes (3): LightType, ParseLightName(), SpriteType

## Knowledge Gaps
- **80 isolated node(s):** `ObjectSelected`, `ObjectDeselected`, `VisibilityChanged`, `PositionChanged`, `RotationChanged` (+75 more)
  These have ≤1 connection - possible missing edges or undocumented components.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Image` connect `GPU Image Core` to `Shader Compilation`, `Descriptor & Shadow Pipeline`, `Swapchain`, `Texture & Material`, `Image & Barrier System`?**
  _High betweenness centrality (0.172) - this node is a cross-community bridge._
- **Why does `ImageData` connect `GPU Image Core` to `Texture & Material`, `Descriptor & Shadow Pipeline`, `Swapchain`?**
  _High betweenness centrality (0.164) - this node is a cross-community bridge._
- **Why does `Environment` connect `Swapchain` to `Shader Struct Parsing`, `GPU Image Core`, `Transform Math`?**
  _High betweenness centrality (0.147) - this node is a cross-community bridge._
- **What connects `ObjectSelected`, `ObjectDeselected`, `VisibilityChanged` to the rest of the system?**
  _80 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Shader Compilation` be split into smaller, more focused modules?**
  _Cohesion score 0.053313587560162905 - nodes in this community are weakly interconnected._
- **Should `Shader Struct Parsing` be split into smaller, more focused modules?**
  _Cohesion score 0.0679563492063492 - nodes in this community are weakly interconnected._
- **Should `Descriptor & Shadow Pipeline` be split into smaller, more focused modules?**
  _Cohesion score 0.05520614954577219 - nodes in this community are weakly interconnected._