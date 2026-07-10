# Graph Report - D:\Projects\Neurus  (2026-07-10)

## Corpus Check
- 81 files · ~2,795 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 559 nodes · 1027 edges · 25 communities
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 11 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Editor & Property UI|Editor & Property UI]]
- [[_COMMUNITY_Application Entry|Application Entry]]
- [[_COMMUNITY_Shader Compilation|Shader Compilation]]
- [[_COMMUNITY_Debug Primitives & Transform|Debug Primitives & Transform]]
- [[_COMMUNITY_Scene & Camera Controllers|Scene & Camera Controllers]]
- [[_COMMUNITY_Swapchain|Swapchain]]
- [[_COMMUNITY_Image & Barrier System|Image & Barrier System]]
- [[_COMMUNITY_Texture & Material|Texture & Material]]
- [[_COMMUNITY_Shader Struct Parsing|Shader Struct Parsing]]
- [[_COMMUNITY_Descriptor & Shadow Pipeline|Descriptor & Shadow Pipeline]]
- [[_COMMUNITY_Input System|Input System]]
- [[_COMMUNITY_Main Window & UI|Main Window & UI]]
- [[_COMMUNITY_Swapchain (alt)|Swapchain (alt)]]
- [[_COMMUNITY_Deterministic RNG|Deterministic RNG]]
- [[_COMMUNITY_Mesh Data Loading|Mesh Data Loading]]
- [[_COMMUNITY_Render Pipeline & SSAO|Render Pipeline & SSAO]]
- [[_COMMUNITY_Render Pipeline & SSAO (alt)|Render Pipeline & SSAO (alt)]]
- [[_COMMUNITY_Render Pipeline & SSAO (alt)|Render Pipeline & SSAO (alt)]]
- [[_COMMUNITY_Descriptor & Buffer Layout|Descriptor & Buffer Layout]]
- [[_COMMUNITY_Pass System|Pass System]]
- [[_COMMUNITY_GPU Image Core|GPU Image Core]]
- [[_COMMUNITY_GeometryPass|GeometryPass]]
- [[_COMMUNITY_Node Graph|Node Graph]]
- [[_COMMUNITY_IBL Push Constants|IBL Push Constants]]
- [[_COMMUNITY_Shadow Eval Constants|Shadow Eval Constants]]

## God Nodes (most connected - your core abstractions)
1. `Image` - 39 edges
2. `Scene` - 37 edges
3. `RenderCache` - 37 edges
4. `DeferredRenderer` - 35 edges
5. `ShaderStruct` - 33 edges
6. `Editor` - 24 edges
7. `Input` - 22 edges
8. `Screenshot` - 22 edges
9. `Light` - 22 edges
10. `Camera` - 21 edges

## Surprising Connections (you probably didn't know these)
- `UploadSunLights()` --references--> `Scene`  [EXTRACTED]
  src/render/passes/LightingPass.cpp → src/editor/Context.cpp
- `GetShadowIntensityArray()` --references--> `Image`  [EXTRACTED]
  src/render/RenderCache.cpp → src/render/DeferredRenderer.cpp
- `FromImage()` --references--> `Image`  [EXTRACTED]
  src/render/Texture.cpp → src/render/DeferredRenderer.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp
- `CreateSSBOLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/ShadowDepthPass.cpp → src/render/DescriptorManager.cpp

## Import Cycles
- None detected.

## Communities (25 total, 0 thin omitted)

### Community 0 - "Editor & Property UI"
Cohesion: 0.05
Nodes (37): _Base, Context, SelectionManager, TypeRegistration, ShaderLib, DefaultScene, Sprite, OutlinerPanel (+29 more)

### Community 1 - "Application Entry"
Cohesion: 0.08
Nodes (38): InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), ResizeViewport(), Run(), StartRenderLoop(), WireSignals() (+30 more)

### Community 2 - "Shader Compilation"
Cohesion: 0.09
Nodes (33): ShaderLibrary, ShaderParser, RenderShader, S_Const, Shader, Compile(), CompileStage(), CreateModule() (+25 more)

### Community 3 - "Debug Primitives & Transform"
Cohesion: 0.06
Nodes (16): DebugLine, DebugPoints, GlmSerialization, Mesh, Transform, UID, PointType, SetOpacity() (+8 more)

### Community 4 - "Scene & Camera Controllers"
Cohesion: 0.08
Nodes (29): CameraPushEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush(), OnCameraRotate() (+21 more)

### Community 5 - "Swapchain"
Cohesion: 0.09
Nodes (25): AttachmentConfig, AttachmentName, IndexBuffer, VertexBuffer, MeshGPU, Screenshot, EnvironmentGPU, AttachmentNameToString() (+17 more)

### Community 6 - "Image & Barrier System"
Cohesion: 0.12
Nodes (35): ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), LoadFromPath(), SaveHDR(), SavePNG(), SwizzleBGRtoRGB() (+27 more)

### Community 7 - "Texture & Material"
Cohesion: 0.10
Nodes (23): Material, MatDataType, MaterialRes, MatParaType, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment() (+15 more)

### Community 8 - "Shader Struct Parsing"
Cohesion: 0.11
Nodes (20): Args, ParaType, ADD_TYPE(), DefFunc(), DefStruct(), GenerateShader(), IsEmpty(), ParseArgs() (+12 more)

### Community 9 - "Descriptor & Shadow Pipeline"
Cohesion: 0.11
Nodes (24): createBuffer(), findMemoryType(), GetBindingDescription(), GetFormatSize(), GetStride(), GPUBuffer(), Map(), StagingBuffer() (+16 more)

### Community 10 - "Input System"
Cohesion: 0.10
Nodes (13): Input, Edit(), GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased() (+5 more)

### Community 11 - "Main Window & UI"
Cohesion: 0.11
Nodes (9): NeurusMainWindow, CreateDocks(), CreateMenus(), getVulkanWidget(), LoadLayout(), makePlaceholder(), NeurusMainWindow(), RestoreDefaultLayout() (+1 more)

### Community 12 - "Swapchain (alt)"
Cohesion: 0.15
Nodes (13): SyncObjects, RenderConfig, DeferredRenderer, sync_fence(), sync_semaphore(), DeferredRenderer(), DrawFrame(), GetLastSwapchainImage() (+5 more)

### Community 13 - "Deterministic RNG"
Cohesion: 0.19
Nodes (10): SSAOPass, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, DeterministicRNG, rng_state, GenerateKernel() (+2 more)

### Community 14 - "Mesh Data Loading"
Cohesion: 0.27
Nodes (12): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetVertexCount(), LoadObj(), LoadObjFromString() (+4 more)

### Community 15 - "Render Pipeline & SSAO"
Cohesion: 0.24
Nodes (7): ComputePipelineBuilder, ComputePass, IBLPass, CreateEquirectSampler(), dispatchCompute(), Generate(), IBLPass()

### Community 16 - "Render Pipeline & SSAO (alt)"
Cohesion: 0.27
Nodes (8): LightingPass, CreatePipeline(), GetLightSSBO(), GetSunLightSSBO(), LightingPass(), UploadSunLights(), WriteDescriptors(), UploadLights()

### Community 17 - "Render Pipeline & SSAO (alt)"
Cohesion: 0.27
Nodes (10): ShadowIntensityPass, DescriptorSetLayoutBuilder, CreateCameraLayout(), CreateSunDescriptorSetLayout(), CreateSunPipeline(), CreateSunShadowSampler(), ShadowIntensityPass(), WriteSunDescriptors() (+2 more)

### Community 18 - "Descriptor & Buffer Layout"
Cohesion: 0.33
Nodes (6): DescriptorManager, Allocate(), Build(), CalculatePoolSizes(), DescriptorSet(), DescriptorSetLayout()

### Community 19 - "Pass System"
Cohesion: 0.44
Nodes (9): Pass, ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth(), PresetClearValues() (+1 more)

### Community 20 - "GPU Image Core"
Cohesion: 0.47
Nodes (9): Swapchain, AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate() (+1 more)

### Community 21 - "GeometryPass"
Cohesion: 0.38
Nodes (6): GeometryPass, BeginPass(), EndPass(), GeometryPass(), Record(), RenderContext

### Community 22 - "Node Graph"
Cohesion: 0.60
Nodes (5): Connect(), Disconnect(), Graph, SocketInT, SocketOutT

### Community 23 - "IBL Push Constants"
Cohesion: 0.40
Nodes (5): IBLPushConstants, maxStep, mipLevel, _pad, roughnessSq

### Community 24 - "Shadow Eval Constants"
Cohesion: 0.50
Nodes (4): SunShadowEvalPushConstants, bias, layerIndex, lightViewProj

## Knowledge Gaps
- **34 isolated node(s):** `_Base`, `EditorEvents`, `EventQueue`, `TypeRegistration`, `DescriptorSetLayoutBuilder` (+29 more)
  These have ≤1 connection - possible missing edges or undocumented components.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `DeferredRenderer` connect `Swapchain (alt)` to `Editor & Property UI`, `Application Entry`, `Swapchain`, `Image & Barrier System`, `Texture & Material`, `Descriptor & Shadow Pipeline`, `Deterministic RNG`, `Render Pipeline & SSAO`, `Render Pipeline & SSAO (alt)`, `Render Pipeline & SSAO (alt)`, `Pass System`, `GPU Image Core`, `GeometryPass`?**
  _High betweenness centrality (0.364) - this node is a cross-community bridge._
- **Why does `Scene` connect `Editor & Property UI` to `Application Entry`, `Debug Primitives & Transform`, `Scene & Camera Controllers`, `Main Window & UI`, `Swapchain (alt)`, `Render Pipeline & SSAO (alt)`?**
  _High betweenness centrality (0.314) - this node is a cross-community bridge._
- **Why does `Editor` connect `Application Entry` to `Editor & Property UI`, `Input System`, `Swapchain (alt)`?**
  _High betweenness centrality (0.154) - this node is a cross-community bridge._
- **What connects `_Base`, `EditorEvents`, `EventQueue` to the rest of the system?**
  _34 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Editor & Property UI` be split into smaller, more focused modules?**
  _Cohesion score 0.05451127819548872 - nodes in this community are weakly interconnected._
- **Should `Application Entry` be split into smaller, more focused modules?**
  _Cohesion score 0.0786308973172988 - nodes in this community are weakly interconnected._
- **Should `Shader Compilation` be split into smaller, more focused modules?**
  _Cohesion score 0.09191583610188261 - nodes in this community are weakly interconnected._