# Graph Report - D:\Projects\Neurus  (2026-08-10)

## Corpus Check
- 120 files · ~4,550 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 910 nodes · 1691 edges · 48 communities (47 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 21 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Shader Compilation (alt)|Shader Compilation (alt)]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Editor & Context|Editor & Context]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Render Cache & Screenshots|Render Cache & Screenshots]]
- [[_COMMUNITY_Debug Primitives & Light|Debug Primitives & Light]]
- [[_COMMUNITY_Transform Math|Transform Math]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_ArrayBuffer|ArrayBuffer]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_LogDelegate|LogDelegate]]
- [[_COMMUNITY_Shadow Eval Constants|Shadow Eval Constants]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Debug Primitives & Light (alt)|Debug Primitives & Light (alt)]]
- [[_COMMUNITY_CodeEditor|CodeEditor]]
- [[_COMMUNITY_Vulkan Context|Vulkan Context]]
- [[_COMMUNITY_Editor|Editor]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_Debug Primitives & Transform (alt)|Debug Primitives & Transform (alt)]]
- [[_COMMUNITY_LogPanel|LogPanel]]
- [[_COMMUNITY_Outliner|Outliner]]
- [[_COMMUNITY_CodeEditor (alt)|CodeEditor (alt)]]
- [[_COMMUNITY_ProfilingRow|ProfilingRow]]
- [[_COMMUNITY_Editor & Property UI (alt)|Editor & Property UI (alt)]]
- [[_COMMUNITY_ShaderHighlighter|ShaderHighlighter]]
- [[_COMMUNITY_Descriptor & Buffer Layout|Descriptor & Buffer Layout]]
- [[_COMMUNITY_GPU Image Core (alt)|GPU Image Core (alt)]]
- [[_COMMUNITY_Debug Primitives & Transform (alt)|Debug Primitives & Transform (alt)]]
- [[_COMMUNITY_ScalarSlider|ScalarSlider]]
- [[_COMMUNITY_LogModel|LogModel]]
- [[_COMMUNITY_MacOSSurface|MacOSSurface]]
- [[_COMMUNITY_ShaderFieldDelegate|ShaderFieldDelegate]]
- [[_COMMUNITY_RenderGraph|RenderGraph]]
- [[_COMMUNITY_Input|Input]]
- [[_COMMUNITY_LogFilterProxy|LogFilterProxy]]
- [[_COMMUNITY_Shader Compilation (alt)|Shader Compilation (alt)]]
- [[_COMMUNITY_Buffer Layout|Buffer Layout]]
- [[_COMMUNITY_SyncObjects|SyncObjects]]

## God Nodes (most connected - your core abstractions)
1. `RenderCache` - 48 edges
2. `Init()` - 43 edges
3. `Editor` - 37 edges
4. `ShaderStruct` - 36 edges
5. `Scene` - 35 edges
6. `Image` - 34 edges
7. `SceneController` - 31 edges
8. `EventQueue` - 25 edges
9. `IOperationSink` - 25 edges
10. `Light` - 24 edges

## Surprising Connections (you probably didn't know these)
- `InitVulkan()` --calls--> `CreatePlatformSurface()`  [INFERRED]
  src/app/Application.cpp → src/platform/PlatformSurface.cpp
- `GetObjectIDs()` --references--> `ObjectID`  [EXTRACTED]
  src/ui/UIContext.cpp → src/editor/controllers/ShaderController.cpp
- `GetLastSwapchainImage()` --references--> `Image`  [EXTRACTED]
  src/render/DeferredRenderer.cpp → src/render/Barrier.cpp
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/Barrier.cpp
- `FromImage()` --references--> `Image`  [EXTRACTED]
  src/render/Texture.cpp → src/render/Barrier.cpp

## Import Cycles
- None detected.

## Communities (48 total, 1 thin omitted)

### Community 0 - "Shader Compilation"
Cohesion: 0.05
Nodes (66): UniformBuffer, ComputeShader, DescriptorSetLayoutBuilder, GPUProfiler, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu (+58 more)

### Community 1 - "Scene & Camera Controllers"
Cohesion: 0.08
Nodes (68): CameraPose, CameraPushEvent, CameraResizeEvent, CameraRotateEvent, CameraSlideEvent, CameraTargetChanged, CameraZoomEvent, NotifyCameraChanged() (+60 more)

### Community 2 - "Editor & Property UI"
Cohesion: 0.06
Nodes (46): AppendDefault(), ApplyFieldEdit(), GetStageUnit(), OnCodeEdited(), OnCodeRestored(), OnCompileShader(), OnCreateShader(), OnFieldAddRestored() (+38 more)

### Community 3 - "GPU Image Core"
Cohesion: 0.10
Nodes (43): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+35 more)

### Community 4 - "Editor & Property UI (alt)"
Cohesion: 0.09
Nodes (38): project(), Serializable, ConfigComponent(), Load(), HistoryComponent(), SceneComponent(), HistoryView, OperationManager (+30 more)

### Community 5 - "Shader Compilation (alt)"
Cohesion: 0.09
Nodes (28): Mesh, TypeRegistration, RenderShader, S_Const, SetObjShader(), MeshBindings, ParseAndGenerate(), Recompile() (+20 more)

### Community 6 - "Shader Struct Parsing"
Cohesion: 0.10
Nodes (20): Args, ShaderEvents, ParaType, ADD_TYPE(), DefFunc(), DefStruct(), ParseArgs(), ParseType() (+12 more)

### Community 7 - "Editor & Context"
Cohesion: 0.13
Nodes (21): BuildProject(), InitEditor(), InitRenderer(), InitVulkan(), NewFrameSignals(), OnProjectNew(), OnProjectOpen(), OnProjectSave() (+13 more)

### Community 8 - "Image & Barrier System"
Cohesion: 0.10
Nodes (25): AccessFlags2, Buffer, createBuffer(), findMemoryType(), GPUBuffer(), Map(), Unmap(), Upload() (+17 more)

### Community 9 - "Texture & Material"
Cohesion: 0.11
Nodes (22): MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData() (+14 more)

### Community 10 - "Render Cache & Screenshots"
Cohesion: 0.13
Nodes (26): AttachmentConfig, Graph, Disconnect(), NodeT, Pass, ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor() (+18 more)

### Community 11 - "Debug Primitives & Light"
Cohesion: 0.09
Nodes (11): setObjectId(), CosineToDeg(), DegToCosine(), LightProperties(), setCutoff(), setOuterCutoff(), setShadowEnabled(), CameraProperties (+3 more)

### Community 12 - "Transform Math"
Cohesion: 0.11
Nodes (13): Camera(), ChangeCamPersp(), ChangeCamRatio(), RecomputeMatrices(), SetPosition(), SetTarPos(), GlmSerialization, GetActiveCamera() (+5 more)

### Community 13 - "Editor & Property UI (alt)"
Cohesion: 0.17
Nodes (21): Controllers, Editor, FinishLoad(), GenerateIBL(), GetContext(), Initialize(), NewScene(), OnCameraAdd() (+13 more)

### Community 14 - "Descriptor & Shadow Pipeline"
Cohesion: 0.11
Nodes (9): GetEnvironmentGPU(), GetMeshGPU(), GetShadowIntensityArray(), UseEnvironmentGPU(), UseMeshGPU(), UploadMesh(), EnvironmentGPU, MeshGPU (+1 more)

### Community 15 - "ArrayBuffer"
Cohesion: 0.14
Nodes (16): ArrayBuffer, GetLightingCache(), SetLightingCache(), UpdateLight(), UpdateLighting(), UpdatePointLight(), UpdatePointLights(), UpdateSpotLight() (+8 more)

### Community 16 - "Debug Primitives & Transform"
Cohesion: 0.14
Nodes (15): Resource, CheckStatus(), GetObjectID(), RebuildObjList(), SnapshotSelectionUids(), UseCamera(), UseDebugLine(), UseDebugPoints() (+7 more)

### Community 17 - "Editor & Property UI (alt)"
Cohesion: 0.15
Nodes (14): ItemFlags, data(), addSection(), buildTree(), flags(), headerData(), nodeString(), sectionTitle() (+6 more)

### Community 18 - "Main Window & UI"
Cohesion: 0.14
Nodes (10): NativeWindowHandle, Win32Surface, UIManager, CreateDocks(), CreateMenus(), getViewportHwnd(), makePlaceholder(), RestoreDefaultLayout() (+2 more)

### Community 19 - "LogDelegate"
Cohesion: 0.22
Nodes (11): sizeHint(), AddButtonRect(), editorEvent(), paint(), setModelData(), updateEditorGeometry(), QSize, LogDelegate (+3 more)

### Community 20 - "Shadow Eval Constants"
Cohesion: 0.20
Nodes (15): ShadowEvalPushConstants, alpha, bias, farPlane, frameCount, jitterX, jitterY, jitterZ (+7 more)

### Community 21 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 22 - "Swapchain"
Cohesion: 0.20
Nodes (10): DeferredRenderer, PipelineSignature, DeferredRenderer(), GetLastSwapchainImage(), HandleResize(), HandleSurfaceChange(), RebuildMainGraph(), recordFrame() (+2 more)

### Community 23 - "Debug Primitives & Light (alt)"
Cohesion: 0.15
Nodes (7): Light, LightType, CreateDefaultScene(), Light(), ParseLightName(), SpriteType, DefaultScene

### Community 24 - "CodeEditor"
Cohesion: 0.24
Nodes (10): CodeEditor(), focusInEvent(), focusOutEvent(), lineNumberAreaWidth(), resizeEvent(), setLineNumbersVisible(), updateLineNumberAreaWidth(), QFocusEvent (+2 more)

### Community 25 - "Vulkan Context"
Cohesion: 0.24
Nodes (10): CreateInstance(), findGraphicsQueueFamily(), findGraphicsQueueFamilyWithPresent(), findTransferQueueFamily(), InitDevice(), InitQueue(), selectPhysicalDeviceIndex(), CreatePlatformSurface() (+2 more)

### Community 26 - "Editor"
Cohesion: 0.20
Nodes (9): Editor(), GetLightGPU(), UseLightGPU(), CreateLightingCache(), UploadEnvironment(), UploadLight(), Environment, LightGPU (+1 more)

### Community 27 - "Editor & Property UI (alt)"
Cohesion: 0.31
Nodes (10): collectExpandedPaths(), indexFromPath(), indexPathOf(), populateSections(), setShowCreateButton(), setShowEmptyState(), ShaderEditorPanel(), QTreeView (+2 more)

### Community 28 - "Debug Primitives & Transform (alt)"
Cohesion: 0.18
Nodes (5): PointType, SetColor(), SetOpacity(), SetPointType(), DebugPoints

### Community 29 - "LogPanel"
Cohesion: 0.27
Nodes (8): BuildToolbar(), LoadLogPanelStyle(), LogPanel(), MaybeScrollToBottom(), OnAutoScrollToggled(), OnClearClicked(), OnPauseToggled(), LogPanel

### Community 30 - "Outliner"
Cohesion: 0.27
Nodes (9): AddCategoryGroup(), EnsureRowPool(), Outliner(), Populate(), UIPanel, Execute(), FrameProfile, Outliner (+1 more)

### Community 31 - "CodeEditor (alt)"
Cohesion: 0.25
Nodes (3): paintEvent(), Viewport(), Viewport

### Community 32 - "ProfilingRow"
Cohesion: 0.33
Nodes (6): ProfilingHead(), ProfilingRow(), setHidden(), QTreeWidget, QTreeWidgetItem, ProfilingRow

### Community 33 - "Editor & Property UI (alt)"
Cohesion: 0.42
Nodes (9): LogBuffer, BuildHeader(), BuildTransformEditor(), BuildTypeSubpanels(), PropertyPanel(), Refresh(), SetEnabled(), ShowTypeSubpanel() (+1 more)

### Community 34 - "ShaderHighlighter"
Cohesion: 0.33
Nodes (8): QTextDocument, Language, ShaderHighlighter, applyCpp(), applyGlsl(), applyJson(), setLanguage(), ShaderHighlighter()

### Community 35 - "Descriptor & Buffer Layout"
Cohesion: 0.33
Nodes (6): Allocate(), Build(), CalculatePoolSizes(), DescriptorSet(), DescriptorSetLayout(), DescriptorManager

### Community 36 - "GPU Image Core (alt)"
Cohesion: 0.47
Nodes (9): AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate(), Swapchain() (+1 more)

### Community 38 - "ScalarSlider"
Cohesion: 0.29
Nodes (6): eventFilter(), ScalarSlider(), setValue(), ScalarSlider, Vec3Spin, QEvent

### Community 39 - "LogModel"
Cohesion: 0.25
Nodes (7): LogEntry, FormatLogTimestamp(), rowCount(), SourceLength(), OnExportClicked(), LogModel, time_point

### Community 40 - "MacOSSurface"
Cohesion: 0.33
Nodes (6): CAMetalLayer, NeurusMetalView, -initWithFrame, -isOpaque, -layerClass, NSView

### Community 41 - "ShaderFieldDelegate"
Cohesion: 0.33
Nodes (5): createEditor(), populateTypeCombo(), setReadOnly(), ShaderFieldRow(), ShaderFieldRow

### Community 42 - "RenderGraph"
Cohesion: 0.29
Nodes (4): Clear(), GetPipeline(), UsePipeline(), PipelineCache

### Community 43 - "Input"
Cohesion: 0.33
Nodes (5): GetModifiers(), GetMouseButton(), Modifiers, MouseButton, Input

### Community 44 - "LogFilterProxy"
Cohesion: 0.33
Nodes (3): LevelFilter, setLevelFilter(), LogFilterProxy

### Community 45 - "Shader Compilation (alt)"
Cohesion: 0.40
Nodes (3): SetOptimizationLevel(), ShaderCompiler(), ShaderCompiler

### Community 46 - "Buffer Layout"
Cohesion: 0.60
Nodes (4): GetBindingDescription(), GetFormatSize(), GetStride(), BufferLayout

### Community 47 - "SyncObjects"
Cohesion: 0.50
Nodes (4): SyncObjects, sync_fence(), sync_semaphore(), device

## Knowledge Gaps
- **82 isolated node(s):** `Serializable`, `JSONOutputArchive`, `JSONInputArchive`, `Modifiers`, `MouseButton` (+77 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Scene` connect `Debug Primitives & Transform` to `Scene & Camera Controllers`, `Editor & Property UI`, `GPU Image Core`, `Editor & Property UI (alt)`, `Debug Primitives & Transform (alt)`, `Transform Math`, `Editor & Property UI (alt)`, `Debug Primitives & Light (alt)`, `Debug Primitives & Transform (alt)`, `Outliner`, `CodeEditor (alt)`?**
  _High betweenness centrality (0.171) - this node is a cross-community bridge._
- **Why does `Editor` connect `Editor & Property UI (alt)` to `Editor & Property UI (alt)`, `Shader Compilation (alt)`, `Editor & Context`, `Transform Math`, `Descriptor & Shadow Pipeline`, `ArrayBuffer`, `Debug Primitives & Transform`, `Swapchain`, `Debug Primitives & Light (alt)`, `Editor`?**
  _High betweenness centrality (0.154) - this node is a cross-community bridge._
- **Why does `ImageData` connect `GPU Image Core` to `Editor & Property UI (alt)`, `Editor & Property UI`, `Texture & Material`, `Debug Primitives & Transform`, `Editor`?**
  _High betweenness centrality (0.126) - this node is a cross-community bridge._
- **What connects `Serializable`, `JSONOutputArchive`, `JSONInputArchive` to the rest of the system?**
  _82 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Shader Compilation` be split into smaller, more focused modules?**
  _Cohesion score 0.05063291139240506 - nodes in this community are weakly interconnected._
- **Should `Scene & Camera Controllers` be split into smaller, more focused modules?**
  _Cohesion score 0.08013640238704177 - nodes in this community are weakly interconnected._
- **Should `Editor & Property UI` be split into smaller, more focused modules?**
  _Cohesion score 0.05647058823529412 - nodes in this community are weakly interconnected._