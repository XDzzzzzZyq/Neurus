# Graph Report - Neurus  (2026-06-28)

## Corpus Check
- 125 files · ~71,097 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1000 nodes · 1455 edges · 89 communities (51 shown, 38 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 12 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `76a56d4a`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Rendering Primitives & Resources|Rendering Primitives & Resources]]
- [[_COMMUNITY_Pipeline & Buffer Construction|Pipeline & Buffer Construction]]
- [[_COMMUNITY_Debug Visualization|Debug Visualization]]
- [[_COMMUNITY_Application Bootstrap|Application Bootstrap]]
- [[_COMMUNITY_Scene Objects & Property UI|Scene Objects & Property UI]]
- [[_COMMUNITY_Camera & Transform Math|Camera & Transform Math]]
- [[_COMMUNITY_SSAO Kernel Sampling|SSAO Kernel Sampling]]
- [[_COMMUNITY_Editor-Renderer Integration|Editor-Renderer Integration]]
- [[_COMMUNITY_Vulkan Image & Pipeline State|Vulkan Image & Pipeline State]]
- [[_COMMUNITY_Attachment & Render Cache|Attachment & Render Cache]]
- [[_COMMUNITY_Buffer Memory Management|Buffer Memory Management]]
- [[_COMMUNITY_Texture & Mipmap Pipeline|Texture & Mipmap Pipeline]]
- [[_COMMUNITY_Mouse & Input State|Mouse & Input State]]
- [[_COMMUNITY_Selection Management|Selection Management]]
- [[_COMMUNITY_Geometry Pass G-Buffer|Geometry Pass G-Buffer]]
- [[_COMMUNITY_GUI & Main Window|GUI & Main Window]]
- [[_COMMUNITY_IBL Environment Lighting|IBL Environment Lighting]]
- [[_COMMUNITY_Shader Module & Program|Shader Module & Program]]
- [[_COMMUNITY_Lighting Compute Shader|Lighting Compute Shader]]
- [[_COMMUNITY_Shadow Depth Cubemap|Shadow Depth Cubemap]]
- [[_COMMUNITY_Event Bus System|Event Bus System]]
- [[_COMMUNITY_Camera Controller Events|Camera Controller Events]]
- [[_COMMUNITY_Swapchain & Presentation|Swapchain & Presentation]]
- [[_COMMUNITY_Render Pass Manager|Render Pass Manager]]
- [[_COMMUNITY_UIEvents Signal Bus|UIEvents Signal Bus]]
- [[_COMMUNITY_Editor Context|Editor Context]]
- [[_COMMUNITY_Vulkan Instance & Surface|Vulkan Instance & Surface]]
- [[_COMMUNITY_NeurusMainWindow Docks|NeurusMainWindow Docks]]
- [[_COMMUNITY_pipelineshaderstagecreateinfo  frome...|pipelineshaderstagecreateinfo / frome...]]
- [[_COMMUNITY_addattribute  getbindingdescription|addattribute / getbindingdescription]]
- [[_COMMUNITY_controllers  neurus|controllers / neurus]]
- [[_COMMUNITY_commandbuffer  dispatch|commandbuffer / dispatch]]
- [[_COMMUNITY_clearvertices  debugline|clearvertices / debugline]]
- [[_COMMUNITY_clearpoints  debugpoints|clearpoints / debugpoints]]
- [[_COMMUNITY_indexbuffer  indexbuffer|indexbuffer / indexbuffer]]
- [[_COMMUNITY_vertexbuffer  vertexbuffer|vertexbuffer / vertexbuffer]]
- [[_COMMUNITY_buildcomputepipeline  computepipelin...|buildcomputepipeline / computepipelin...]]
- [[_COMMUNITY_neurus  raii|neurus / raii]]
- [[_COMMUNITY_gpuname  setgpuname|gpuname / setgpuname]]
- [[_COMMUNITY_buildgraphicspipeline  device|buildgraphicspipeline / device]]
- [[_COMMUNITY_parsepath  sprite|parsepath / sprite]]
- [[_COMMUNITY_shaderprogram  device|shaderprogram / device]]
- [[_COMMUNITY_namespace  neurusmainwindow|namespace / neurusmainwindow]]
- [[_COMMUNITY_neurus  vulkancontext|neurus / vulkancontext]]
- [[_COMMUNITY_neurus  imagedata|neurus / imagedata]]
- [[_COMMUNITY_neurus  meshdata|neurus / meshdata]]
- [[_COMMUNITY_neurus  buffer|neurus / buffer]]
- [[_COMMUNITY_neurus  bufferlayout|neurus / bufferlayout]]
- [[_COMMUNITY_neurus  gpubuffer|neurus / gpubuffer]]
- [[_COMMUNITY_neurus  indexbuffer|neurus / indexbuffer]]
- [[_COMMUNITY_neurus  stagingbuffer|neurus / stagingbuffer]]
- [[_COMMUNITY_neurus  vertexbuffer|neurus / vertexbuffer]]
- [[_COMMUNITY_neurus  cameracontroller|neurus / cameracontroller]]
- [[_COMMUNITY_parameters  neurus|parameters / neurus]]
- [[_COMMUNITY_types  neurus|types / neurus]]
- [[_COMMUNITY_neurus  input|neurus / input]]
- [[_COMMUNITY_neurus  selectioncontroller|neurus / selectioncontroller]]
- [[_COMMUNITY_cameraevents  neurus|cameraevents / neurus]]
- [[_COMMUNITY_neurus  uievents|neurus / uievents]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_neurus  namespace|neurus / namespace]]
- [[_COMMUNITY_namespace  project|namespace / project]]
- [[_COMMUNITY_namespace  mainwindow|namespace / mainwindow]]
- [[_COMMUNITY_namespace  outlinerpanel|namespace / outlinerpanel]]
- [[_COMMUNITY_namespace  vulkanwidget|namespace / vulkanwidget]]
- [[_COMMUNITY_Community 86|Community 86]]
- [[_COMMUNITY_Community 88|Community 88]]
- [[_COMMUNITY_Community 89|Community 89]]
- [[_COMMUNITY_Community 90|Community 90]]
- [[_COMMUNITY_Community 91|Community 91]]
- [[_COMMUNITY_Community 96|Community 96]]

## God Nodes (most connected - your core abstractions)
1. `AspectFromFormat()` - 15 edges
2. `Image()` - 14 edges
3. `Device` - 12 edges
4. `createFromPixelData()` - 12 edges
5. `CaptureAllAttachments()` - 11 edges
6. `Initialize()` - 11 edges
7. `LoadObjFromString()` - 11 edges
8. `FromFile()` - 10 edges
9. `Mesh` - 9 edges
10. `FromImageData()` - 9 edges

## Surprising Connections (you probably didn't know these)
- `ExportShadowDepthEquirect()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/DeferredRenderer.cpp → src/render/DescriptorManager.cpp
- `CreateLightLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/ShadowDepthPass.cpp → src/render/DescriptorManager.cpp
- `createUniforms()` --calls--> `DescriptorPool()`  [INFERRED]
  src/render/passes/ShadowDepthPass.cpp → src/render/DescriptorManager.cpp
- `CreateCameraLayout()` --calls--> `BuildLayout()`  [INFERRED]
  src/render/passes/GeometryPass.cpp → src/render/DescriptorManager.cpp
- `WriteDescriptors()` --references--> `ImageView`  [EXTRACTED]
  src/render/passes/IBLPass.cpp → src/render/passes/GeometryPass.cpp

## Import Cycles
- None detected.

## Communities (89 total, 38 thin omitted)

### Community 0 - "Rendering Primitives & Resources"
Cohesion: 0.09
Nodes (24): neurus(), ClearValue, ImageView, BeginPass(), CreateCameraLayout(), CreatePipeline(), EndPass(), GeometryPass() (+16 more)

### Community 1 - "Pipeline & Buffer Construction"
Cohesion: 0.06
Nodes (44): ComputePipelineBuilder, Image, CreateDescriptorSetLayout(), CreateEquirectSampler(), CreatePipeline(), dispatchCompute(), Generate(), IBLPass() (+36 more)

### Community 2 - "Debug Visualization"
Cohesion: 0.06
Nodes (22): mutable, PointType, SetColor(), SetColor(), SetPointType(), neurus(), cereal(), neurus() (+14 more)

### Community 3 - "Application Bootstrap"
Cohesion: 0.06
Nodes (45): InitEditor(), InitRenderer(), InitVulkan(), LoadProject(), resolveResourcePath(), Run(), StartRenderLoop(), WireSignals() (+37 more)

### Community 4 - "Scene Objects & Property UI"
Cohesion: 0.10
Nodes (31): Light, QDoubleSpinBox, QModelIndex, CreateDefaultScene(), Scene, shared_ptr, string, QWidget (+23 more)

### Community 5 - "Camera & Transform Math"
Cohesion: 0.07
Nodes (24): mat4, GenFloatData(), GetProjectionMatrix(), GetViewMatrix(), SetCamPos(), SetTarPos(), BuildIBLTextures(), CreateCubemapSampler() (+16 more)

### Community 6 - "SSAO Kernel Sampling"
Cohesion: 0.09
Nodes (26): array, KernelSampleGpu, kMaxKernelSamples, kNoiseEntryCount, NoiseEntryGpu, CreateDescriptorSetLayout(), CreatePipeline(), DeterministicRNG (+18 more)

### Community 7 - "Editor-Renderer Integration"
Cohesion: 0.11
Nodes (29): DeferredRenderer, Edit(), Editor(), GenerateIBL(), Initialize(), OnCameraAdd(), OnIBLLoad(), OnLightAdd() (+21 more)

### Community 8 - "Vulkan Image & Pipeline State"
Cohesion: 0.08
Nodes (51): CommandBuffer, DescriptorSetLayout, DescriptorSetLayoutBuilder, Device, Extent2D, GPUBuffer, ImageAspectFlags, ImageData (+43 more)

### Community 9 - "Attachment & Render Cache"
Cohesion: 0.09
Nodes (30): AttachmentConfig, AttachmentName, Format, AttachmentNameToString(), ConfigFor(), createAttachment(), HasAttachment(), RenderCache() (+22 more)

### Community 10 - "Buffer Memory Management"
Cohesion: 0.09
Nodes (24): createBuffer(), findMemoryType(), GetDescriptorInfo(), GPUBuffer(), Unmap(), Upload(), StagingBuffer(), Unmap() (+16 more)

### Community 11 - "Texture & Mipmap Pipeline"
Cohesion: 0.20
Nodes (25): DeviceSize, computeMipLevels(), createFromPixelData(), createSampler(), ForAttachment(), FromData(), FromFile(), FromImage() (+17 more)

### Community 13 - "Mouse & Input State"
Cohesion: 0.10
Nodes (11): GetInputState(), IsAltHeld(), IsCtrlHeld(), IsMouseButtonClicked(), IsMouseButtonPressed(), IsMouseButtonReleased(), IsShiftHeld(), RecordMousePress() (+3 more)

### Community 14 - "Selection Management"
Cohesion: 0.12
Nodes (16): BoxSelect(), RaycastSelect(), RayIntersectsSphere(), ScreenToRay(), EventQueue, neurus(), EventQueue(), GetEventQueue() (+8 more)

### Community 15 - "Geometry Pass G-Buffer"
Cohesion: 0.28
Nodes (12): AttachmentLoadOp, AttachmentStoreOp, ColorAttachmentCount(), ColorLoadOpFor(), ColorStoreOpFor(), DepthLoadOpFor(), DepthStoreOpFor(), HasDepth() (+4 more)

### Community 16 - "GUI & Main Window"
Cohesion: 0.18
Nodes (20): PresentModeKHR, AcquireNextImage(), chooseExtent(), choosePresentMode(), chooseSurfaceFormat(), createImageViews(), Present(), Recreate() (+12 more)

### Community 17 - "IBL Environment Lighting"
Cohesion: 0.22
Nodes (16): CreateLightLayout(), createMultiviewColorPipeline(), createMultiviewPipeline(), createSingleFacePipeline(), createUniforms(), Record(), ShadowDepthPass(), updateUBO() (+8 more)

### Community 18 - "Shader Module & Program"
Cohesion: 0.19
Nodes (17): DescriptorBindingFlags, DescriptorImageInfo, DescriptorPoolSize, DescriptorSetLayoutBinding, DescriptorType, Allocate(), Build(), CalculatePoolSizes() (+9 more)

### Community 19 - "Lighting Compute Shader"
Cohesion: 0.28
Nodes (17): AddFace(), ComputeCenter(), ComputeFaceNormals(), ComputeTangents(), GetIndexCount(), GetMeshCenter(), GetMeshName(), GetVertexCount() (+9 more)

### Community 20 - "Shadow Depth Cubemap"
Cohesion: 0.30
Nodes (14): ChannelCount(), ConvertHalfToU8(), EnsureDirectory(), HalfToFloat(), ImageData(), IsBGRFormat(), LoadFromPath(), PixelByteSize() (+6 more)

### Community 22 - "Event Bus System"
Cohesion: 0.19
Nodes (14): CreateInstance(), debugCallback(), findGraphicsQueueFamily(), getRequiredInstanceExtensions(), initDevice(), selectPhysicalDeviceIndex(), VulkanContext(), Instance (+6 more)

### Community 23 - "Camera Controller Events"
Cohesion: 0.15
Nodes (8): LightType, pair, Light(), ParseLightName(), SetColor(), SpriteType, string, vec3

### Community 24 - "Swapchain & Presentation"
Cohesion: 0.19
Nodes (11): activeScene(), GetActiveCamera(), GetObjectID(), GetObjectIDs(), SetScene(), Camera, EditorContext(), ObjectID (+3 more)

### Community 25 - "Render Pass Manager"
Cohesion: 0.25
Nodes (7): CreateDescriptorSets(), DispatchCompute(), neurus(), neurus(), namespace, vk, namespace

### Community 26 - "UIEvents Signal Bus"
Cohesion: 0.13
Nodes (19): MatDataType, MaterialRes, MatParaType, InitParamData(), LoadMaterial(), Material(), ParseConfig(), SetMatParam() (+11 more)

### Community 27 - "Editor Context"
Cohesion: 0.30
Nodes (11): CameraPushEvent, CameraRotateEvent, CameraSlideEvent, CameraZoomEvent, Init(), NotifyCameraChanged(), OnCameraPush(), OnCameraRotate() (+3 more)

### Community 28 - "Vulkan Instance & Surface"
Cohesion: 0.18
Nodes (6): CheckStatus(), GetActiveCamera(), GetObjectID(), SceneModifStatus, Camera, ObjectID

### Community 29 - "NeurusMainWindow Docks"
Cohesion: 0.24
Nodes (8): _Base, neurus(), RenderContext(), hovered_object(), class, Context(), EditorContext(), namespace

### Community 30 - "pipelineshaderstagecreateinfo / frome..."
Cohesion: 0.31
Nodes (9): PipelineShaderStageCreateInfo, FromEmbedded(), FromFile(), GetStageInfo(), ShaderModule(), ShaderStageFlagBits, Device, string (+1 more)

### Community 31 - "addattribute / getbindingdescription"
Cohesion: 0.48
Nodes (6): AddAttribute(), GetBindingDescription(), GetFormatSize(), GetStride(), Format, VertexInputBindingDescription

### Community 32 - "controllers / neurus"
Cohesion: 0.33
Nodes (5): neurus(), neurus(), project(), namespace, namespace

### Community 33 - "commandbuffer / dispatch"
Cohesion: 0.33
Nodes (5): neurus(), SetScissor(), SetViewport(), namespace, vk

### Community 34 - "clearvertices / debugline"
Cohesion: 0.38
Nodes (4): PushDebugLine(), PushDebugLines(), vec3, vector

### Community 35 - "clearpoints / debugpoints"
Cohesion: 0.38
Nodes (4): PushDebugPoint(), PushDebugPoints(), vec3, vector

### Community 36 - "indexbuffer / indexbuffer"
Cohesion: 0.40
Nodes (5): IndexBuffer(), Device, DeviceSize, PhysicalDevice, Queue

### Community 37 - "vertexbuffer / vertexbuffer"
Cohesion: 0.40
Nodes (5): VertexBuffer(), Device, DeviceSize, PhysicalDevice, Queue

### Community 38 - "buildcomputepipeline / computepipelin..."
Cohesion: 0.50
Nodes (4): BuildComputePipeline(), ComputePipelineBuilder(), Device, Pipeline

### Community 39 - "neurus / raii"
Cohesion: 0.67
Nodes (3): neurus(), raii(), namespace

### Community 40 - "gpuname / setgpuname"
Cohesion: 0.83
Nodes (3): gpuName(), setGpuName(), QString

### Community 41 - "buildgraphicspipeline / device"
Cohesion: 0.67
Nodes (3): BuildGraphicsPipeline(), Device, Pipeline

### Community 43 - "shaderprogram / device"
Cohesion: 0.67
Nodes (3): ShaderProgram(), Device, Extent2D

### Community 44 - "namespace / neurusmainwindow"
Cohesion: 0.67
Nodes (3): namespace, ads(), neurus()

### Community 86 - "Community 86"
Cohesion: 0.24
Nodes (8): Mesh(), ReleaseGPUBuffers(), ReloadMeshData(), UploadToGPU(), Device, PhysicalDevice, Queue, string

### Community 88 - "Community 88"
Cohesion: 0.25
Nodes (7): Fence, m_fence(), m_semaphore(), neurus(), Semaphore, device, namespace

### Community 89 - "Community 89"
Cohesion: 0.48
Nodes (6): ImageState, ToVulkanImageState(), Transition(), Image, VkCommandBuffer, VulkanImageState

### Community 90 - "Community 90"
Cohesion: 0.06
Nodes (37): Camera, CameraUBOData, class, CommandPool, GeometryRenderItem, Mesh, buildRenderItem(), computeCameraData() (+29 more)

## Knowledge Gaps
- **228 isolated node(s):** `GPUBuffer`, `DescriptorSetLayout`, `Pipeline`, `PhysicalDevice`, `Queue` (+223 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **38 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ImageData` connect `Vulkan Image & Pipeline State` to `Attachment & Render Cache`, `Community 90`, `Texture & Mipmap Pipeline`?**
  _High betweenness centrality (0.094) - this node is a cross-community bridge._
- **Why does `Environment` connect `Editor-Renderer Integration` to `Vulkan Image & Pipeline State`, `Community 90`, `Camera & Transform Math`?**
  _High betweenness centrality (0.080) - this node is a cross-community bridge._
- **Why does `Mesh` connect `Community 90` to `Community 86`, `Scene Objects & Property UI`, `Selection Management`?**
  _High betweenness centrality (0.057) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `CaptureAllAttachments()` (e.g. with `AttachmentNameToString()` and `AttachmentNameToString()`) actually correct?**
  _`CaptureAllAttachments()` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `GPUBuffer`, `DescriptorSetLayout`, `Pipeline` to the rest of the system?**
  _228 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Rendering Primitives & Resources` be split into smaller, more focused modules?**
  _Cohesion score 0.08866995073891626 - nodes in this community are weakly interconnected._
- **Should `Pipeline & Buffer Construction` be split into smaller, more focused modules?**
  _Cohesion score 0.05851063829787234 - nodes in this community are weakly interconnected._