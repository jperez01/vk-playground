# Vulkan Logic Modernization Plan
> Deep analysis of the actual codebase — every finding references a specific file, function, and current code pattern.

---

## Executive Summary

This plan was produced by auditing every relevant source file. It prioritizes **correctness bugs and performance anti-patterns that exist today** before moving to advanced Vulkan features. The most critical issues are:

1. **Every image barrier uses `ALL_COMMANDS + MEMORY_WRITE` — the broadest possible stall** (`vk_images.cpp`)
2. **A new heap buffer is allocated per frame for GPU scene data** (`vk_engine.cpp:draw_geometry`)
3. **`immediate_submit()` blocks the entire graphics queue** for every mesh and texture upload
4. **The post-process pass doesn't sample the draw image** (`screen_texture.frag` outputs `inTexCoords` not the texture)
5. **Assimp models use `OpaqueSurfaces[0].material` as a hardcoded pipeline** — crashes if no glTF is loaded
6. **No pipeline cache anywhere** — every cold start recompiles everything
7. **Binary semaphores + a fence per frame** instead of timeline semaphores
8. **Zero debug labels, zero GPU timestamps** anywhere in the codebase

---

## 1. Synchronization & Barriers

### 1.1 Over-broad Image Barriers in `transition_image`
**Priority: High**
**File:** `shared/vk_images.cpp` → `vkutil::transition_image()`

**Current code:**
```cpp
VkImageMemoryBarrier2 imageBarrier{};
imageBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
imageBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
```

This single function is called for **every** layout transition: compute→color attachment, color attachment→transfer src, transfer dst→shader read, etc. Using `ALL_COMMANDS` as both source and destination forces the GPU to drain the entire pipeline regardless of what stages are actually involved.

**Fix:** Accept explicit stage/access masks at the call site, or create named transition helpers:
```cpp
namespace vkutil {
    // Compute write → fragment sample
    void transition_for_sampling(VkCommandBuffer cmd, VkImage image);
    // Undefined → transfer dst (for uploads)
    void transition_for_upload(VkCommandBuffer cmd, VkImage image);
    // Color attachment → transfer src (for copy-to-swapchain)
    void transition_for_copy_src(VkCommandBuffer cmd, VkImage image);
    // Generic version still available but forces explicit masks
    void transition_image(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                          VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);
}
```

**All call sites in `vk_engine.cpp` `draw_main()` and `draw()` must be updated to pass meaningful stage flags.**

---

### 1.2 Replace Binary Semaphores + Fence with Timeline Semaphore
**Priority: Medium**
**File:** `src/vk_engine.h` → `FrameData`; `src/vk_engine.cpp` → `init_sync_structures()`, `draw()`

**Current structure:**
```cpp
struct FrameData {
    VkSemaphore _swapchainSemaphore;   // binary — image acquired
    VkSemaphore _renderSemaphore;       // binary — render finished
    VkFence     _renderFence;           // CPU blocks here waiting for frame N-2
    // ...
};
```

Three synchronization primitives per frame can be collapsed into one timeline semaphore per frame:

```cpp
struct FrameData {
    VkSemaphore _timelineSemaphore;     // timeline, value = frameNumber
    VkSemaphore _swapchainSemaphore;    // still needed for present (binary)
    // _renderSemaphore and _renderFence removed
    uint64_t    _frameTimelineValue{0};
    // ...
};
```

This also enables async compute and transfer queues to signal the same timeline, giving fine-grained overlap without adding more semaphores.

---

### 1.3 `vkDeviceWaitIdle` on Every Resize
**Priority: Medium**
**File:** `src/vk_engine.cpp` → `resize_swapchain()`

```cpp
void VulkanEngine::resize_swapchain() {
    vkDeviceWaitIdle(device);  // stalls everything
    destroy_swapchain();
    // ...
}
```

**Fix:** Only wait for frames that are actively using swapchain images. With a timeline semaphore (see §1.2), wait for the `frameTimelineValue` of the most recently submitted frame instead of idling the whole device.

---

## 2. Per-Frame Memory Allocation Anti-Patterns

### 2.1 New Heap Buffer Every Frame for Scene Data
**Priority: High**
**File:** `src/vk_engine.cpp` → `draw_geometry()` (~line 480)

**Current code:**
```cpp
AllocatedBuffer gpuSceneDataBuffer = vkutil::createBuffer(
    allocator, sizeof(GPUSceneData),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VMA_MEMORY_USAGE_CPU_TO_GPU);   // ← new VMA allocation every frame

get_current_frame()._deletionQueue.push_function([=, this]() {
    vkutil::destroyBuffer(allocator, gpuSceneDataBuffer);  // ← free it next frame
});
```

This invokes VMA's allocator on **every rendered frame**, defeating the purpose of a memory allocator. For a 256-byte uniform buffer this is pure overhead.

**Fix:** Allocate `FRAME_OVERLAP` persistent mapped buffers at init time:
```cpp
// In FrameData:
struct FrameData {
    // ...
    AllocatedBuffer sceneDataBuffer;  // persistent, CPU_TO_GPU mapped
};

// In init_sync_structures() or a new init_frame_resources():
for (auto& frame : frames) {
    frame.sceneDataBuffer = vkutil::createBuffer(
        allocator, sizeof(GPUSceneData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
}

// In draw_geometry() — just memcpy:
GPUSceneData* sceneDataPtr = (GPUSceneData*)get_current_frame().sceneDataBuffer.info.pMappedData;
*sceneDataPtr = sceneData;
```

---

### 2.2 `immediate_submit()` Blocks the Graphics Queue
**Priority: High**
**File:** `src/vk_engine.cpp` → `immediate_submit()`, `uploadMesh()`, `create_image()`

```cpp
void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
    VK_CHECK(vkResetFences(device, 1, &immFence));
    // ... record ...
    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, immFence));
    VK_CHECK(vkWaitForFences(device, 1, &immFence, true, 9999999999));  // ← full stall
}
```

Called inside `uploadMesh()` and `create_image()`, which are called from:
- `init_default_data()` (at startup — acceptable stall)
- `sendModelDataToGpu()` (called at runtime via the file-open dialog — **unacceptable stall during rendering**)
- `loadGltf()` callbacks

**Fix:**
1. Keep a dedicated transfer queue (request one from vk-bootstrap with `get_queue(vkb::QueueType::transfer)`).
2. Move uploads to a `UploadQueue` class that batches staging → GPU copies into a single submit per frame boundary.
3. For runtime loads (file dialog), show a loading state and process the upload at the start of the next frame before `vkAcquireNextImage`, not mid-render.

---

### 2.3 Staging Buffers Are Never Pooled
**Priority: Medium**
**File:** `src/vk_engine.cpp` → `create_image()`, `uploadMesh()`

Both functions create a one-shot `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` staging buffer, use it once, then destroy it via `immediate_submit`'s cleanup. For repeated asset loads this generates significant allocator churn.

**Fix:** Implement a simple `StagingRingBuffer`:
```cpp
class StagingRingBuffer {
    VkBuffer     buffer;
    VmaAllocation allocation;
    uint8_t*     mapped;
    size_t       capacity;
    size_t       writeOffset{0};
public:
    std::byte*   reserve(size_t size, size_t alignment = 256);
    VkDeviceSize offsetOf(std::byte* ptr) const;
    void         reset();  // call once per frame after uploads complete
};
```

---

## 3. Pipeline Management

### 3.1 No Pipeline Cache Anywhere
**Priority: High**
**Files:** `src/vk_engine.cpp` → `init_background_pipelines()`, `init_post_process_pipeline()`; `shared/vk_pipelines.cpp` → `PipelineBuilder::build_pipeline()`; `src/vk_engine.cpp` (GLTFMetallic_Roughness pipelines)

**Current calls:**
```cpp
vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline);
// ... and in PipelineBuilder::build_pipeline():
vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline);
```

`VK_NULL_HANDLE` for the cache means **zero reuse between runs**. On cold start every pipeline is compiled from scratch by the driver.

**Fix:**
```cpp
class PipelineCacheManager {
    VkPipelineCache cache{VK_NULL_HANDLE};
    VkDevice        device;
public:
    void init(VkDevice dev, const std::filesystem::path& cachePath);
    void save(const std::filesystem::path& cachePath);
    VkPipelineCache get() const { return cache; }
    ~PipelineCacheManager() { if (cache) vkDestroyPipelineCache(device, cache, nullptr); }
};
```

- Pass `PipelineCacheManager::get()` everywhere `VK_NULL_HANDLE` is used today.
- Save cache to `pipeline_cache.bin` on `VulkanEngine::cleanup()`.
- Load on `init_pipelines()`.
- Invalidate by storing driver version and GPU UUID in a small header.

---

### 3.2 `sendModelDataToGpu()` Leaks `VkDescriptorSetLayout`
**Priority: High**
**File:** `src/vk_engine.cpp` → `sendModelDataToGpu()` (~line 1316)

```cpp
void VulkanEngine::sendModelDataToGpu(Model& model) {
    for (...) {
        VkDescriptorSetLayout textureLayout;
        DescriptorLayoutBuilder layoutBuilder;
        layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        // ... more bindings ...
        textureLayout = layoutBuilder.build(device, VK_SHADER_STAGE_FRAGMENT_BIT);
        // ← textureLayout is NEVER DESTROYED
        // ...
    }
}
```

Every call to `sendModelDataToGpu()` (triggered by the file-open dialog) leaks one `VkDescriptorSetLayout` per material. Additionally this layout is **identical for every material** — it doesn't need to be created more than once.

**Fix:** Create one shared `VkDescriptorSetLayout` for Assimp material textures during `init_descriptors()` and reuse it. Destroy it in `cleanup()` or the main deletion queue.

---

### 3.3 Expand Dynamic State to Reduce Pipeline Variants
**Priority: Medium**
**File:** `shared/vk_pipelines.cpp` → `PipelineBuilder::build_pipeline()`

Currently cull mode and front face are baked into every pipeline. Adding these to dynamic state cuts the required number of pipeline objects and eliminates variants for two-sided vs. one-sided materials:

```cpp
std::vector<VkDynamicState> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_CULL_MODE,               // add
    VK_DYNAMIC_STATE_FRONT_FACE,              // add
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT,   // add
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,  // add
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT,    // add
};
```

---

## 4. Rendering Bugs & Correctness Issues

### 4.1 Post-Process Pass Doesn't Sample the Draw Image
**Priority: Critical**
**File:** `shaders/screen_texture.frag`

```glsl
// Current:
layout (location = 0) out vec4 outFragColor;
// ...
void main() {
    outFragColor = vec4(inTexCoords, 1.0f, 1.0f);  // ← outputs UV coordinates, not the image!
}
```

The post-process pass binds `postProcessDescriptorSet` (which contains `drawImage`) but the fragment shader ignores it and outputs the UV coordinates directly. The `postProcessingImage` is written but never blit to the swapchain — `draw()` blits `drawImage` to swapchain instead.

**Fix:**
```glsl
layout (set = 0, binding = 0) uniform sampler2D screenTexture;
layout (location = 0) in vec2 inTexCoords;
layout (location = 0) out vec4 outFragColor;

void main() {
    outFragColor = texture(screenTexture, inTexCoords);
    // Apply tonemapping, color grading, etc. here
}
```

Also fix `draw()` in `vk_engine.cpp` to blit `postProcessingImage` → swapchain instead of `drawImage` → swapchain.

---

### 4.2 Assimp Models Hardcoded to `OpaqueSurfaces[0]` Pipeline
**Priority: Critical**
**File:** `src/vk_engine.cpp` → `draw_geometry()` (~line 517)

```cpp
auto someData = drawCommands.OpaqueSurfaces[0];  // ← undefined behavior if no glTF loaded

for (Model& model : importedModels) {
    for (unsigned int i = 0; i < model.meshes.size(); i++) {
        // ...
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            someData.material->pipeline->layout,  // ← uses glTF pipeline for Assimp model
            1, 1, &currentMaterialInfo.materialSet, 0, nullptr);
        // ...
        vkCmdPushConstants(cmd, someData.material->pipeline->layout, ...);
        vkCmdDrawIndexed(cmd, meshData.indices.size(), 1, 0, 0, 0);
    }
}
```

This crashes if no glTF scene is loaded, uses the wrong pipeline layout for Assimp material descriptor sets, and bypasses frustum culling entirely.

**Fix:** Give Assimp `Model` its own `MaterialInstance` pointing to a pipeline created specifically for its descriptor set layout. Alternatively, unify both pipelines and push Assimp `RenderObject`s into `DrawContext::OpaqueSurfaces` through the same path as glTF assets.

---

### 4.3 Render Scale Ignored in `draw_main()`
**Priority: Medium**
**File:** `src/vk_engine.cpp` → `draw_main()`

```cpp
// Compute dispatch uses windowExtent not drawExtent:
vkCmdDispatch(cmd, std::ceil(windowExtent.width / 16.0), std::ceil(windowExtent.height / 16.0), 1);

// Viewport also uses windowExtent:
viewport.width  = (float)windowExtent.width;
viewport.height = (float)windowExtent.height;

// But drawImage is sized to drawExtent:
// drawImage is created with drawExtent (renderScale * windowExtent)
```

When `renderScale != 1.0`, the compute dispatch covers too many tiles (if scale > 1) or too few (if scale < 1), and the viewport doesn't match the actual draw image size.

**Fix:** Replace all `windowExtent` references inside `draw_main()` with `drawExtent`.

---

### 4.4 `postProcessingImage` Not Recreated on Swapchain Resize
**Priority: Medium**
**File:** `src/vk_engine.cpp` → `resize_swapchain()`

`resize_swapchain()` calls `create_swapchain()` which recreates `drawImage` and `depthImage` at the new size, but `postProcessingImage` is only created in `init_post_process_pipeline()` and is never resized. After a window resize, the post-process image remains at the original resolution.

**Fix:** Extract post-process image creation into a helper `create_post_process_images(uint32_t w, uint32_t h)` and call it from both `init_post_process_pipeline()` and `resize_swapchain()`.

---

## 5. Shader Inconsistencies & Duplication

### 5.1 `SceneData` Declared in Four Different Places
**Priority: Medium**
**Files:** `shaders/scene_data.glsl`, `shaders/basic.frag`, `shaders/basic.vert`, `shaders/input_structures.glsl`

`scene_data.glsl` provides the canonical `SceneData` struct as a descriptor set binding, but `basic.frag` and `basic.vert` re-declare a minimal inline version with different field names. `input_structures.glsl` includes `scene_data.glsl` but other shaders include it selectively.

**Fix:** Every shader that needs scene data should use `#include "scene_data.glsl"`. Remove inline re-declarations. Enforce this by auditing all `.vert`/`.frag`/`.comp` files for `layout(set` declarations that duplicate the shared struct.

---

### 5.2 Duplicated SH Irradiance Code in PBR Shaders
**Priority: Low**
**Files:** `shaders/mesh.frag`, `shaders/mesh_pbr.frag`

Both shaders contain their own copy of the spherical harmonics irradiance calculation. Any change to the lighting model requires updating both files.

**Fix:** Move shared PBR math into `pbr_header.glsl` (which already exists) and have both shaders include it. `mesh_pbr.frag` already references `pbr_header.glsl` — extend it to cover the shared irradiance code.

---

### 5.3 Shader File Proliferation with No Variant System
**Priority: Low**
**Files:** `shaders/tri_mesh_descriptors.vert`, `tri_mesh_pushconstants.vert`, `tri_mesh_ssbo.vert`, `tri_mesh.vert`

Four nearly-identical vertex shaders exist for different data sourcing strategies (descriptors, push constants, SSBO, base). These should be one shader with specialization constants or `#define` guards managed by the build system.

**Fix:** Consolidate into one `tri_mesh.vert` using `#ifdef USE_PUSH_CONSTANTS` etc., compiled into separate SPIRV modules by the CMake shader build rule with different preprocessor flags.

---

### 5.4 `bUseValidationLayers` Hardcoded to `true`
**Priority: Medium**
**File:** `src/vk_engine.cpp` (top of file)

```cpp
constexpr bool bUseValidationLayers = true;
```

Validation layers impose ~10–20% CPU overhead and should not be enabled in release builds.

**Fix:**
```cpp
#ifdef NDEBUG
constexpr bool bUseValidationLayers = false;
#else
constexpr bool bUseValidationLayers = true;
#endif
```

---

## 6. Debug & Profiling Infrastructure (Currently Zero)

### 6.1 No Debug Labels on Any Pass
**Priority: High**
**File:** `src/vk_engine.cpp` → `draw()`, `draw_main()`, `draw_geometry()`

There are no `vkCmdBeginDebugUtilsLabelEXT` calls anywhere. Opening this project in RenderDoc or NSight produces an unlabeled flat list of commands with no structure.

**Fix:** Create a RAII `DebugLabel` scope helper and wrap every pass:
```cpp
struct DebugLabel {
    VkCommandBuffer cmd;
    DebugLabel(VkCommandBuffer c, const char* name, glm::vec4 color = {1,1,0,1}) : cmd(c) {
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        std::copy_n(&color[0], 4, label.color);
        vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
    }
    ~DebugLabel() { vkCmdEndDebugUtilsLabelEXT(cmd); }
};
```

Also name all persistent Vulkan objects with `vkSetDebugUtilsObjectNameEXT` during initialization (queues, pipelines, descriptor pools, images).

---

### 6.2 No GPU Timestamps
**Priority: High**
**File:** Create `src/profiling/gpu_profiler.h/cpp`

The `EngineStats` struct in `vk_engine.h` has `mesh_draw_time` but it is measured with `std::chrono` on the **CPU side**, which doesn't account for asynchronous GPU execution.

**Fix:** Create a `GPUProfiler` using `VkQueryPool` with `VK_QUERY_TYPE_TIMESTAMP`:
```cpp
class GPUProfiler {
    VkQueryPool pool;
    uint32_t    queryIndex{0};
    float       timestampPeriod;  // from VkPhysicalDeviceLimits
public:
    void beginRegion(VkCommandBuffer cmd, const char* name);
    void endRegion  (VkCommandBuffer cmd);
    void collectResults(VkDevice device);  // call after vkWaitForFences
    void renderImGui();                     // display in overlay
};
```

Each frame: reset pool → begin/end around each pass → collect results after GPU completes → display in ImGui.

---

### 6.3 Enable Full Validation Layer Features
**Priority: Low**
**File:** `src/vk_engine.cpp` → `init_vulkan()`

Currently only the default validation layer is enabled. Add:
```cpp
VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
VkValidationFeatureEnableEXT enables[] = {
    VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,  // optional, expensive
};
validationFeatures.enabledValidationFeatureCount = 2;
validationFeatures.pEnabledValidationFeatures = enables;
// Chain into VkInstanceCreateInfo::pNext
```

Synchronization validation will immediately catch the over-broad barriers in §1.1.

---

## 7. Descriptor System

### 7.1 Per-Material Descriptor Sets for Assimp Models
**Priority: Medium**
**File:** `src/vk_engine.cpp` → `sendModelDataToGpu()`

Currently each Assimp material gets a descriptor set allocated from `globalDescriptorAllocator`. The layout leak (§3.2) means this function cannot be called more than once without accumulating leaked layouts. Once the layout leak is fixed, the descriptor set allocation itself is fine for a first pass.

**Long-term fix:** Migrate to a bindless texture approach. All texture `VkImageView`s are registered in a single large descriptor array at set 0, binding 0. Shaders receive texture indices via push constants. This eliminates all per-material descriptor sets:

```glsl
// Bindless textures (set 0, binding 0)
layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant) uniform PushConstants {
    mat4     worldMatrix;
    uint64_t vertexBuffer;
    uint     albedoIndex;
    uint     normalIndex;
    uint     metalRoughIndex;
} pc;
```

Requires `VK_EXT_descriptor_indexing` (already a Vulkan 1.2 core feature — check that `shaderSampledImageArrayNonUniformIndexing` and `descriptorBindingPartiallyBound` are requested in `init_vulkan()`).

---

### 7.2 Optimize Descriptor Pool Ratios
**Priority: Low**
**File:** `src/vk_engine.cpp` → `init_descriptors()`

The pool ratios in `DescriptorAllocatorGrowable::init()` should reflect actual usage. Add instrumentation to count descriptor allocations per type per frame (see §6.2) before tuning these values.

---

## 8. Asset Pipeline

### 8.1 Two Parallel Rendering Paths Are Diverging
**Priority: High**
**Files:** `src/vk_engine.cpp`, `src/vk_loader.cpp`, `src/model/model.cpp`

The project has two completely separate asset pipelines that render through different code paths:

| Feature | glTF (fastgltf) | OBJ/FBX (Assimp) |
|---|---|---|
| GPU upload | `uploadMesh` + `create_image` | `sendModelDataToGpu` |
| Draw submission | `DrawContext::OpaqueSurfaces` | Inline loop in `draw_geometry()` |
| Frustum culling | Yes (`is_visible`) | **No** |
| Material system | `GLTFMetallic_Roughness` | Hardcoded to `OpaqueSurfaces[0].material` |
| Pipeline | Correct per-material | Always uses first glTF surface's pipeline |

**Fix:** Define a single `Renderable` interface with a `submitDrawCommands(DrawContext&)` method. Both `LoadedGLTF` and `Model` implement it. `draw_geometry()` processes only the `DrawContext` — it has no knowledge of how renderables populated it.

---

### 8.2 Mesh Optimization Not Applied
**Priority: Medium**
**Files:** `src/vk_engine.cpp` → `uploadMesh()`; `src/vk_loader.cpp`; `src/model/model.cpp`

No vertex cache optimization, overdraw reduction, or vertex fetch optimization is applied to any mesh at load time.

**Fix:** Integrate `meshoptimizer` (add to `vcpkg.json`):
```cpp
#include <meshoptimizer.h>

// After loading indices/vertices:
meshopt_optimizeVertexCache(indices.data(), indices.data(), indexCount, vertexCount);
meshopt_optimizeOverdraw(indices.data(), indices.data(), indexCount,
    &vertices[0].position.x, vertexCount, sizeof(Vertex), 1.05f);
meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indexCount,
    vertices.data(), vertexCount, sizeof(Vertex));
```

---

### 8.3 Shader Loading Has No Error Recovery
**Priority: Low**
**File:** `shared/vk_pipelines.cpp` → `vkutil::load_shader_module()`; `src/vk_engine.cpp` → `init_background_pipelines()`

```cpp
if (!vkutil::load_shader_module("gradient_color.comp", device, &gradientShader)) {
    fmt::print("Error when building the compute shader \n");
}
// Execution continues regardless — gradientShader is uninitialized VkShaderModule
```

If the spirv file is missing, `gradientShader` is an uninitialized handle and the subsequent `vkCreateComputePipelines` call will produce a validation error or crash.

**Fix:** Return `std::expected<VkShaderModule, std::string>` and propagate the error up to `init_pipelines()`, which should abort initialization cleanly rather than continuing with null handles.

---

## 9. GPU-Driven Rendering (Future Phase)

Once the correctness and performance anti-patterns above are fixed, the following are the highest-leverage advanced improvements:

### 9.1 Indirect Draw with GPU Culling
**Priority: Medium (Phase 3)**

Move `is_visible()` to a compute shader that reads all `RenderObject`s from a GPU buffer and writes a compact `VkDrawIndexedIndirectCommand` buffer. The render loop becomes:

```cpp
// Phase A — dispatch culling compute
vkCmdDispatch(cmd, (objectCount + 63) / 64, 1, 1);
// Barrier: SSBO write → indirect read
// Phase B — single indirect draw call
vkCmdDrawIndexedIndirectCount(cmd, indirectBuffer, 0, countBuffer, 0, maxObjects, stride);
```

This eliminates all per-object CPU draw call overhead and scales to 100K+ objects.

---

### 9.2 IBL for PBR Materials
**Priority: Medium (Phase 3)**
**Files:** `shaders/mesh_pbr.frag`, `shaders/pbr_header.glsl`

The current PBR shader uses hardcoded ambient color from `GPUSceneData`. Add:
- Irradiance cubemap for diffuse IBL
- Pre-filtered environment map for specular IBL
- BRDF LUT for split-sum approximation

These are baked offline from an HDR environment map and loaded as `AllocatedImage` resources.

---

### 9.3 Compressed Texture Formats
**Priority: Medium (Phase 3)**
**File:** `src/vk_loader.cpp` → `load_image()`

All textures are loaded as `VK_FORMAT_R8G8B8A8_UNORM`. Query `VkFormatProperties` for BC7 (desktop) or ASTC (mobile) support and load pre-compressed KTX2/DDS files instead. Reduces VRAM usage by 4–8× with minimal quality loss for most textures.

---

## Implementation Roadmap

### Phase 1 — Correctness (Weeks 1–3)
These are bugs or anti-patterns that affect correctness today:

| # | Task | File(s) | Impact |
|---|---|---|---|
| 1 | Fix post-process shader to sample draw image | `screen_texture.frag`, `vk_engine.cpp:draw()` | Post-processing actually works |
| 2 | Fix `OpaqueSurfaces[0]` hardcoded pipeline for Assimp | `vk_engine.cpp:draw_geometry()` | No crash without glTF scene |
| 3 | Fix render scale ignored in `draw_main()` | `vk_engine.cpp:draw_main()` | Correct output at non-1.0 scale |
| 4 | Fix `postProcessingImage` not resized on resize | `vk_engine.cpp:resize_swapchain()` | Correct output after resize |
| 5 | Fix `sendModelDataToGpu` descriptor layout leak | `vk_engine.cpp:sendModelDataToGpu()` | No Vulkan object leak |
| 6 | Fix `bUseValidationLayers` for release builds | `vk_engine.cpp` | No validation overhead in release |
| 7 | Fix shader error handling (abort on missing SPIRV) | `vk_pipelines.cpp`, `vk_engine.cpp` | Clean init failure |

---

### Phase 2 — Performance (Weeks 4–6)
These reduce runtime overhead without changing rendering output:

| # | Task | File(s) | Impact |
|---|---|---|---|
| 1 | Persistent scene data uniform buffers | `vk_engine.cpp:draw_geometry()` | Eliminate per-frame VMA alloc |
| 2 | Pipeline cache with disk serialization | `vk_pipelines.cpp`, `vk_engine.cpp` | Faster cold-start compilation |
| 3 | Scope image barriers precisely | `vk_images.cpp:transition_image()` | Fewer GPU pipeline stalls |
| 4 | Staging buffer pool | `vk_engine.cpp`, new `staging_manager.h` | Less allocator churn |
| 5 | Add debug labels to all passes | `vk_engine.cpp:draw_main()` | RenderDoc / NSight usability |
| 6 | Add GPU timestamp queries | New `gpu_profiler.h` | Accurate GPU timing |
| 7 | Expand dynamic pipeline state | `vk_pipelines.cpp` | Fewer pipeline objects |

---

### Phase 3 — Modernization (Weeks 7–12)
These adopt modern Vulkan patterns and improve rendering quality:

| # | Task | File(s) | Impact |
|---|---|---|---|
| 1 | Timeline semaphores | `vk_engine.h`, `vk_engine.cpp` | Simpler sync, enables async |
| 2 | Async upload queue | `vk_engine.cpp`, new `upload_queue.h` | No frame stalls during loads |
| 3 | Unify glTF + Assimp render paths | `vk_engine.cpp`, `model.cpp`, `vk_loader.cpp` | Single maintained code path |
| 4 | Bindless texture descriptor array | `vk_descriptors.h`, shaders | Eliminate descriptor switching |
| 5 | GPU-driven indirect culling | New `culling.comp`, `vk_engine.cpp` | Scale to large scenes |
| 6 | Mesh optimization at load time | `vk_loader.cpp`, `model.cpp` | Better GPU vertex cache use |
| 7 | IBL for PBR | `mesh_pbr.frag`, `pbr_header.glsl` | Higher quality lighting |
| 8 | Compressed texture loading (BC7/KTX2) | `vk_loader.cpp` | 4–8× texture VRAM savings |

---

## Performance Targets

| Metric | Current (Estimated) | Target | How to Measure |
|---|---|---|---|
| GPU frame time (1080p, test scene) | Unknown (CPU-measured only) | < 2 ms | GPU timestamps (§6.2) |
| VMA allocations per frame | ~3 (scene buffer + staging) | 0 (pre-allocated) | VMA stats callback |
| Pipeline creation time (cold start) | ~200ms | < 20ms (cached) | CPU `std::chrono` |
| Image barriers issuing ALL_COMMANDS stalls | Every transition | Zero | Sync validation |
| Draw calls for Assimp models | Bypasses culling | Subject to frustum culling | Stats overlay |
| Debug label coverage | 0% | 100% of passes | RenderDoc capture |

---

## Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|
| Post-process fix reveals broken tonemapping | Medium | High | Accept as known tech debt; fix tonemapping in same PR |
| Assimp pipeline unification breaks material rendering | High | Medium | Keep Assimp path behind a compile flag until tested |
| Barrier scoping introduces new GPU sync bugs | High | Low | Enable `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` before changing barriers |
| Pipeline cache invalidation on driver update causes stalls | Medium | Low | Store driver version in cache header; delete stale cache on version mismatch |
| Timeline semaphore changes break frame pacing | High | Low | Keep binary semaphore path behind `#ifdef USE_TIMELINE_SEMAPHORES` during transition |
| Bindless migration requires all shaders to be rewritten | Medium | High | Migrate one material type at a time; keep old path alive |

---

## Appendix: Useful Libraries to Add to `vcpkg.json`

| Library | Purpose | Priority |
|---|---|---|
| `meshoptimizer` | Vertex cache/overdraw optimization, LOD | Phase 3 |
| `ktx` (KTX-Software) | BC7/ASTC compressed texture loading | Phase 3 |
| `spirv-reflect` | Reflect descriptor layouts from SPIRV | Phase 3 |
| `shaderc` | Runtime GLSL→SPIRV for shader hot-reload | Phase 3 |

`volk` (meta-loader, eliminates dispatch table indirection) is worth adding in Phase 2 if extension loading complexity increases.

---

## Appendix: Recommended Tools

| Tool | Use |
|---|---|
| **RenderDoc** | Frame capture — will immediately surface the unlabeled passes (§6.1) and the broken post-process pass (§4.1) |
| **NVIDIA Nsight Graphics** / **AMD RGP** | GPU timeline visualization; quantify barrier stalls from §1.1 |
| **VMA Statistics** (`vmaSetAllocatorCreateInfo` with `pfnAllocate` callback) | Measure per-frame allocation count to validate §2.1 fix |
| **Vulkan Configurator** (vkconfig) | Enable sync validation layer without code changes during development |

### Current State
- Uses Vulkan 1.3 features (dynamic rendering)
- Mix of older and newer Vulkan patterns
- Unknown which optional extensions are being used
- No explicit feature detection and fallback

### Modernization Actions

#### 1.1 Adopt Vulkan 1.3 Core Features
**Priority: High**

**Files Affected:**
- `src/vk_engine.cpp` (init_vulkan)
- `shared/vk_initializers.cpp`

**Action Items:**
- Enable and use VK_KHR_synchronization2 (already seems to be in use)
- Adopt VK_EXT_extended_dynamic_state3 for more flexible pipelines
- Use VK_KHR_dynamic_rendering (already in use) consistently
- Enable VK_EXT_descriptor_indexing for bindless textures
- Consider VK_KHR_buffer_device_address (already using device addresses)

#### 1.2 Feature Detection and Graceful Degradation
**Priority: Medium**

**Files Affected:**
- Create `src/vulkan/feature_detector.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Query available features at startup
- Store feature availability flags
- Implement fallback paths for optional features
- Log which features are enabled/disabled
- Document minimum required features vs. optional enhancements

#### 1.3 Extension Management
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.cpp`

**Action Items:**
- Create structured extension management system
- Group extensions by category (debugging, performance, features)
- Only enable extensions when actually needed
- Validate extension dependencies
- Consider using volk for extension loading instead of vk_bootstrap for more control

---

## 2. Descriptor Management

### Current State
- Custom descriptor allocator system (DescriptorAllocatorGrowable)
- Material descriptors allocated per-material
- Per-frame descriptor allocation
- No descriptor indexing/bindless approach

### Modernization Actions

#### 2.1 Bindless Texture System
**Priority: High**

**Files Affected:**
- `shared/vk_descriptors.h/cpp`
- `src/vk_engine.h/cpp`
- Shader files (mesh.frag, textured_lit.frag)

**Action Items:**
- Implement bindless texture array using descriptor indexing:
  ```cpp
  // Single descriptor set with large texture array
  layout(set = 0, binding = 0) uniform sampler2D textures[];
  
  // Pass texture indices via push constants or vertex data
  layout(push_constant) uniform PushConstants {
      uint textureIndex;
  };
  ```
- Create global texture descriptor set with large array (1000+ textures)
- Allocate texture slots from a central registry
- Pass texture indices instead of binding per-material descriptors
- Reduces descriptor set switching during rendering
- Enables easier texture streaming

#### 2.2 Descriptor Buffer Extension
**Priority: Medium** (Vulkan 1.3+ / Extension)

**Files Affected:**
- `shared/vk_descriptors.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Investigate VK_EXT_descriptor_buffer for even better performance
- Descriptor buffers treat descriptors as raw memory
- Eliminates descriptor set allocation overhead
- GPU reads descriptors directly from buffers
- Provides more control over descriptor memory layout

#### 2.3 Optimize Descriptor Pool Sizing
**Priority: Low**

**Files Affected:**
- `shared/vk_descriptors.cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Profile actual descriptor usage per type
- Adjust pool size ratios based on real usage
- Implement dynamic pool growth with better heuristics
- Add telemetry for descriptor pool statistics
- Consider separate pools for different update frequencies

---

## 3. Pipeline Management

### Current State
- Runtime pipeline creation
- No pipeline caching
- Limited dynamic state usage
- PipelineBuilder helper class
- Material pipelines created separately for opaque/transparent

### Modernization Actions

#### 3.1 Pipeline Caching and Serialization
**Priority: High**

**Files Affected:**
- Create `src/vulkan/pipeline_cache.h/cpp`
- `src/vk_engine.cpp`
- `shared/vk_pipelines.cpp`

**Action Items:**
- Implement VkPipelineCache with disk serialization:
  ```cpp
  class PipelineCacheManager {
      VkPipelineCache cache;
      void saveToFile(const std::filesystem::path& path);
      void loadFromFile(const std::filesystem::path& path);
  };
  ```
- Save pipeline cache to disk on shutdown
- Load pipeline cache on startup to speed up pipeline creation
- Version cache files to invalidate on driver updates
- Consider pipeline binary formats for distribution

#### 3.2 Graphics Pipeline Library (GPL)
**Priority: Medium** (VK_EXT_graphics_pipeline_library)

**Files Affected:**
- `shared/vk_pipelines.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Split monolithic pipelines into reusable components:
  - Pre-rasterization: Vertex input, vertex shader, tessellation
  - Fragment shader: Fragment shader only
  - Fragment output: Blending, color write
- Cache pipeline libraries for reuse across similar pipelines
- Dramatically reduces compilation time for pipeline variants
- Enable faster runtime pipeline creation

#### 3.3 Dynamic State Expansion
**Priority: Medium**

**Files Affected:**
- `shared/vk_pipelines.cpp`
- `src/vk_engine.cpp` (draw loops)

**Action Items:**
- Enable more dynamic state to reduce pipeline variants:
  ```cpp
  VK_DYNAMIC_STATE_CULL_MODE
  VK_DYNAMIC_STATE_FRONT_FACE
  VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE
  VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE
  VK_DYNAMIC_STATE_DEPTH_COMPARE_OP
  ```
- Use VkCmdSet* functions in render loop
- Reduces number of pipeline objects needed
- Allows material parameters to control render state without pipeline switching

#### 3.4 Shader Object Extension
**Priority: Low** (VK_EXT_shader_object - very new)

**Files Affected:**
- Extensive changes

**Action Items:**
- Investigate VK_EXT_shader_object as alternative to pipelines
- More flexible than traditional pipelines
- Better for dynamic effects and shader hot-reloading
- May not be widely supported yet - evaluate hardware support first

---

## 4. Memory Management

### Current State
- VMA (Vulkan Memory Allocator) integration ✓
- Some buffer creation helpers
- Manual staging buffer management
- No memory aliasing or sub-allocation optimization
- Per-frame resource allocation patterns

### Modernization Actions

#### 4.1 Unified Buffer Management System
**Priority: High**

**Files Affected:**
- Create `src/resources/buffer_allocator.h/cpp`
- `src/vk_engine.cpp`
- `src/utils/util.cpp`

**Action Items:**
- Create buffer manager with typed allocators:
  ```cpp
  class BufferAllocator {
  public:
      // Linear allocator for per-frame data
      TransientAllocation allocateTransient(size_t size, size_t alignment);
      
      // Pooled allocator for uniform buffers
      UniformBufferAllocation allocateUniform(size_t size);
      
      // Ring buffer for dynamic vertex data
      DynamicAllocation allocateDynamic(size_t size);
  };
  ```
- Implement ring buffers for per-frame resources
- Pool uniform buffers instead of allocating per-use
- Reduce allocator calls by sub-allocating from larger blocks
- Reset transient allocations at frame boundaries

#### 4.2 Optimize Vertex/Index Buffer Layout
**Priority: Medium**

**Files Affected:**
- `shared/vk_types.h` (GPUMeshBuffers)
- `src/vk_engine.cpp` (uploadMesh)
- `src/vk_loader.cpp`

**Action Items:**
- Combine vertex and index buffers into single allocations:
  ```cpp
  struct MeshBuffers {
      VkBuffer buffer;  // Single buffer
      VkDeviceAddress vertexAddress;
      VkDeviceAddress indexAddress;
  };
  ```
- Reduces number of buffer objects
- Better memory locality
- Fewer bind calls in render loop
- Use buffer device address for vertex fetching

#### 4.3 Staging Buffer Pool
**Priority: Medium**

**Files Affected:**
- Create `src/vulkan/staging_manager.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Implement reusable staging buffer pool:
  ```cpp
  class StagingManager {
      std::vector<VkBuffer> stagingBuffers;
      VkBuffer acquireStaging(VkDeviceSize size);
      void releaseStaging(VkBuffer buffer, uint64_t frameIndex);
  };
  ```
- Reuse staging buffers across frames
- Implement double/triple buffering for staging resources
- Batch transfers to reduce submission overhead
- Profile transfer queue usage vs. graphics queue

#### 4.4 Memory Budget Tracking
**Priority: Low**

**Files Affected:**
- Create `src/resources/memory_budget.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Query VK_EXT_memory_budget for available VRAM
- Track allocation sizes per category (textures, buffers, etc.)
- Warn when approaching memory limits
- Implement streaming system for large assets
- Add telemetry/debug UI for memory usage

---

## 5. Command Buffer Management

### Current State
- Fixed command pools per frame
- Command buffer recording per frame
- Some immediate submit functionality
- Manual synchronization

### Modernization Actions

#### 5.1 Command Buffer Abstraction
**Priority: Medium**

**Files Affected:**
- Create `src/vulkan/command_manager.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Create command buffer manager with RAII helpers:
  ```cpp
  class CommandBuffer {
      VkCommandBuffer handle;
  public:
      CommandBuffer(VkCommandPool pool);
      ~CommandBuffer(); // Auto-end if recording
      
      void begin();
      void end();
      RenderPassScope beginRendering(const RenderingInfo& info);
  };
  
  class RenderPassScope {
      ~RenderPassScope() { vkCmdEndRendering(); }
  };
  ```
- Automatically manage command buffer lifecycle
- Prevent forgetting to end recording
- Enable hierarchical command recording

#### 5.2 Secondary Command Buffers
**Priority: Low**

**Files Affected:**
- `src/vk_engine.cpp` (draw loops)
- Create parallel recording infrastructure

**Action Items:**
- Use secondary command buffers for parallel recording:
  - Record different scene regions in parallel
  - Record UI in separate thread
  - Execute secondary buffers in primary
- Requires thread-safe descriptor allocation
- Profile whether parallelism benefits your workload
- Consider task-based parallelism (std::async, thread pool)

#### 5.3 Command Buffer Reuse
**Priority: Low**

**Files Affected:**
- `src/vk_engine.cpp`

**Action Items:**
- Implement command buffer reuse for static geometry
- Record static scene geometry once, replay multiple frames
- Update only dynamic elements per frame
- Use vkCmdExecuteCommands to execute pre-recorded buffers
- Benefits limited if scene changes every frame

---

## 6. Synchronization

### Current State
- VkFence for frame synchronization
- VkSemaphore for swapchain/render synchronization
- Using VkSubmitInfo2 (synchronization2) ✓
- Some timeline semaphore potential

### Modernization Actions

#### 6.1 Timeline Semaphores
**Priority: High**

**Files Affected:**
- `src/vk_engine.h` (FrameData)
- `src/vk_engine.cpp` (synchronization code)

**Action Items:**
- Replace binary semaphores with timeline semaphores:
  ```cpp
  struct FrameSync {
      VkSemaphore timelineSemaphore;
      uint64_t frameNumber;
      
      void wait(uint64_t waitValue);
      void signal(uint64_t signalValue);
  };
  ```
- Enable better GPU-GPU synchronization
- Eliminate need for multiple binary semaphores
- Simplify dependency management
- Enable async compute integration
- Allow CPU to wait for specific GPU workloads

#### 6.2 Fine-Grained Pipeline Barriers
**Priority: Medium**

**Files Affected:**
- `shared/vk_images.cpp` (transition_image)
- `src/vk_engine.cpp` (draw_main)

**Action Items:**
- Use synchronization2 barrier structure fully:
  ```cpp
  VkImageMemoryBarrier2 barrier{
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
      // ...
  };
  ```
- Specify exact pipeline stages instead of broad stages
- Reduce unnecessary GPU stalls
- Profile barriers to ensure they're not overly conservative
- Consider async compute synchronization

#### 6.3 Remove Unnecessary Synchronization
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.cpp`

**Action Items:**
- Audit all vkQueueWaitIdle and vkDeviceWaitIdle calls
- Replace with proper semaphore/fence synchronization
- Remove host-side synchronization in hot paths
- Use memory barriers instead of global barriers where possible
- Profile GPU idle time to identify over-synchronization

---

## 7. Rendering Techniques

### Current State
- Compute shader background effects
- Dynamic rendering API
- Forward rendering with opaque/transparent separation
- PBR materials (metallic-roughness)
- Simple post-processing pass
- Frustum culling on CPU

### Modernization Actions

#### 7.1 GPU-Driven Rendering
**Priority: High**

**Files Affected:**
- Create `shaders/culling.comp`
- Create `src/rendering/gpu_driven.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Move frustum culling to GPU compute shader:
  ```glsl
  // Compute shader reads all draw commands
  // Writes visible draws to indirect buffer
  layout(set = 0, binding = 0) buffer DrawCommands { DrawCommand draws[]; };
  layout(set = 0, binding = 1) buffer IndirectCommands { VkDrawIndexedIndirectCommand indirect[]; };
  layout(set = 0, binding = 2) buffer VisibleCount { uint count; };
  ```
- Use vkCmdDrawIndexedIndirect for rendering
- Occlusion culling with Hi-Z buffer (advanced)
- LOD selection on GPU
- Benefits: Reduces CPU overhead, enables massive scenes
- CPU-side culling is acceptable for smaller scenes but GPU scales better

#### 7.2 Mesh Shader Pipeline
**Priority: Low** (VK_EXT_mesh_shader)

**Files Affected:**
- New shader files
- `shared/vk_pipelines.cpp`

**Action Items:**
- Investigate mesh shaders as replacement for vertex+geometry pipeline
- Mesh shaders offer more flexibility than traditional vertex processing
- Enable better LOD systems and procedural geometry
- Not widely supported on all hardware yet
- Good for modern GPUs (NVIDIA Turing+, AMD RDNA2+)

#### 7.3 Enhance PBR Implementation
**Priority: Medium**

**Files Affected:**
- `shaders/mesh_pbr.frag`
- `shaders/pbr_header.glsl`
- `src/vk_engine.cpp`

**Action Items:**
- Add IBL (Image-Based Lighting):
  - Generate irradiance and prefiltered environment maps
  - BRDF LUT for split-sum approximation
  - Skybox rendering from environment map
- Improve material system:
  - Support clear coat, anisotropy, sheen
  - Implement KHR_materials_* glTF extensions
- Add proper tonemapping in post-process
- Implement better shadow mapping (cascaded shadow maps, PCF)

#### 7.4 Deferred or Hybrid Rendering
**Priority: Medium**

**Files Affected:**
- Extensive changes
- Create G-buffer setup

**Action Items:**
- Consider deferred rendering for scenes with many lights:
  - Render geometry to G-buffer (albedo, normal, roughness/metallic, depth)
  - Light accumulation pass reads G-buffer
  - Reduces per-light overhead for many lights
- Or hybrid: deferred for opaques, forward for transparents
- Trade-offs: Higher memory bandwidth, MSAA complications
- Evaluate based on target scenes and lighting complexity

#### 7.5 Temporal Anti-Aliasing (TAA)
**Priority: Low**

**Files Affected:**
- Post-processing shaders and pipeline
- `src/vk_engine.cpp`

**Action Items:**
- Implement TAA for better anti-aliasing quality:
  - Jitter projection matrix per frame
  - Reproject previous frame
  - Blend with current frame
- Requires motion vectors
- Better quality than MSAA with less cost
- Needs ghost artifact mitigation

---

## 8. Texture & Image Management

### Current State
- STB image loading
- Basic mipmap generation
- Per-texture samplers
- No compression
- No streaming

### Modernization Actions

#### 8.1 Compressed Texture Support
**Priority: High**

**Files Affected:**
- `src/vk_loader.cpp` (load_image)
- Asset pipeline

**Action Items:**
- Support GPU-compressed formats:
  - BC7 for high-quality color (desktop)
  - ASTC for mobile
  - BC5 for normal maps
  - BC4 for single-channel (roughness, metallic)
- Pre-compress textures during asset build
- Reduces memory usage by 4-8x
- Faster loading times
- Use compressonator, basis universal, or similar tools

#### 8.2 Virtual Texturing / Sparse Textures
**Priority: Low** (Advanced)

**Files Affected:**
- Extensive changes

**Action Items:**
- Implement virtual texturing for very large textures:
  - VK_EXT_sparse_residency
  - Stream texture tiles on demand
  - Enables huge texture sizes (8K+, terrain textures)
- Complex system, consider cost/benefit
- Alternative: texture atlasing with smart packing

#### 8.3 Async Texture Loading
**Priority: Medium**

**Files Affected:**
- `src/vk_loader.cpp`
- Create texture streaming system

**Action Items:**
- Load textures asynchronously on separate threads
- Use transfer queue for GPU uploads if available
- Stream textures in background while rendering
- Display lower-resolution mips while high-res loads
- Implement texture priority system

#### 8.4 Sampler Management
**Priority: Low**

**Files Affected:**
- `src/vk_engine.h/cpp`
- `src/vk_loader.cpp`

**Action Items:**
- Create global sampler pool with common configurations
- Use immutable samplers in descriptor layouts where appropriate
- Separate samplers from texture descriptors (samplerless textures)
- Reduces descriptor updates
- Better with bindless textures

---

## 9. Shader Compilation & Management

### Current State
- Compile-time GLSL to SPIRV using glslangValidator
- Shaders loaded from .spv files at runtime
- No shader hot-reloading
- No shader variants/specialization constants

### Modernization Actions

#### 9.1 Shader Hot-Reloading
**Priority: Medium**

**Files Affected:**
- Create `src/shaders/shader_manager.h/cpp`
- `shared/vk_pipelines.cpp`

**Action Items:**
- Implement file watching for shader source changes
- Recompile SPIRV on change
- Rebuild affected pipelines
- Swap pipelines atomically at frame boundary
- Speeds up shader iteration during development
- Consider using shaderc library for runtime compilation

#### 9.2 Specialization Constants
**Priority: Medium**

**Files Affected:**
- Shader files
- `shared/vk_pipelines.cpp`

**Action Items:**
- Use specialization constants for shader configuration:
  ```cpp
  layout(constant_id = 0) const int MAX_LIGHTS = 16;
  layout(constant_id = 1) const bool USE_NORMAL_MAPPING = true;
  ```
- Set constants at pipeline creation time
- Compiler can optimize based on constant values
- Reduces need for preprocessor directives and shader variants
- Creates shader permutations efficiently

#### 9.3 Shader Includes and Module System
**Priority: Low**

**Files Affected:**
- All shader files
- Build system

**Action Items:**
- Already using some includes (input_structures.glsl, scene_data.glsl) ✓
- Organize shaders into reusable modules
- Create common function library (lighting, math utilities)
- Use consistent naming and conventions
- Consider SPIR-V linking for separable shader modules

#### 9.4 Shader Reflection & Automation
**Priority: Low**

**Files Affected:**
- Create shader reflection tool
- Build system

**Action Items:**
- Use SPIRV-Reflect or similar to inspect compiled shaders
- Auto-generate descriptor set layouts from shaders
- Verify shader interfaces match C++ structures
- Catch mismatches at build time
- Generate push constant structures

---

## 10. Debugging & Profiling

### Current State
- Validation layers enabled in debug
- Basic FPS/frametime stats
- Some debug messenger setup
- Manual timing measurements

### Modernization Actions

#### 10.1 Enhanced Debug Markers
**Priority: High**

**Files Affected:**
- All command buffer recording code
- Create debug marker helpers

**Action Items:**
- Use VK_EXT_debug_utils throughout:
  ```cpp
  VkDebugUtilsLabelEXT label{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
      .pLabelName = "Shadow Map Pass",
      .color = {1.0f, 0.0f, 0.0f, 1.0f}
  };
  vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
  // ... rendering ...
  vkCmdEndDebugUtilsLabelEXT(cmd);
  ```
- Name all Vulkan objects with vkSetDebugUtilsObjectNameEXT
- Create RAII helper for scoped markers
- Essential for RenderDoc, NSight, PIX debugging
- Makes GPU captures readable

#### 10.2 GPU Timestamps and Profiling
**Priority: High**

**Files Affected:**
- Create `src/profiling/gpu_profiler.h/cpp`
- `src/vk_engine.cpp`

**Action Items:**
- Implement VkQueryPool for GPU timestamps:
  ```cpp
  class GPUProfiler {
      VkQueryPool queryPool;
      void beginRegion(VkCommandBuffer cmd, const char* name);
      void endRegion(VkCommandBuffer cmd);
      std::vector<TimingResult> getResults();
  };
  ```
- Measure GPU time for each render pass
- Display in ImGui overlay
- Track frame-to-frame variance
- Identify performance bottlenecks
- Store historical data for graphs

#### 10.3 Statistics and Instrumentation
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.h` (EngineStats)
- Create comprehensive stats system

**Action Items:**
- Track more detailed statistics:
  - Draw calls, triangles per pass
  - State changes (pipeline, descriptor binds)
  - Memory allocations per frame
  - Descriptor updates
  - Command buffer count and size
- Add VK_QUERY_TYPE_PIPELINE_STATISTICS support
- Create profiling UI dashboard
- Log stats to file for analysis

#### 10.4 Validation Layer Best Practices
**Priority: Low**

**Files Affected:**
- `src/vk_engine.cpp` (init_vulkan)

**Action Items:**
- Enable validation layer features:
  - VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
  - VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT
  - VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
- Configure validation to catch more issues
- Add custom validation disables where false positives occur
- Test with multiple IHV validation layers (NVIDIA, AMD)

---

## 11. Swapchain & Presentation

### Current State
- Basic swapchain setup with vk-bootstrap
- Manual resize handling
- Present mode not clearly configurable
- No adaptive sync consideration

### Modernization Actions

#### 11.1 Improved Swapchain Management
**Priority: Medium**

**Files Affected:**
- `src/vk_engine.cpp` (swapchain functions)
- Create `src/vulkan/swapchain.h/cpp`

**Action Items:**
- Encapsulate swapchain logic in dedicated class:
  ```cpp
  class SwapchainManager {
      void create(const SwapchainConfig& config);
      void recreate();
      Result<uint32_t> acquireNextImage();
      Result<> present(uint32_t imageIndex);
  };
  ```
- Handle swapchain recreation more robustly
- Support multiple present modes (mailbox, fifo, immediate)
- Allow runtime present mode switching
- Handle surface loss gracefully

#### 11.2 HDR Display Support
**Priority: Low**

**Files Affected:**
- `src/vk_engine.cpp`
- Shaders (tonemapping)

**Action Items:**
- Query display HDR capabilities
- Use VK_COLOR_SPACE_HDR10_ST2084_EXT when available
- Render to wider color gamut (FP16 render targets)
- Apply appropriate tonemapping for HDR vs SDR
- Add HDR brightness controls

#### 11.3 Variable Rate Shading
**Priority: Low** (VK_KHR_fragment_shading_rate)

**Files Affected:**
- Pipeline setup
- Render pass configuration

**Action Items:**
- Use VRS to reduce shading rate in periphery
- Foveated rendering for VR
- Improves performance with minimal quality loss
- Requires hardware support (modern GPUs)

---

## 12. Multi-Threading

### Current State
- Single-threaded rendering
- Some potential for parallel asset loading
- No explicit multi-threading support

### Modernization Actions

#### 12.1 Async Asset Loading
**Priority: High**

**Files Affected:**
- `src/vk_loader.cpp`
- `src/model/model.cpp`
- Create task system

**Action Items:**
- Load models and textures on background threads:
  ```cpp
  std::future<Model> loadModelAsync(const std::string& path) {
      return std::async(std::launch::async, [path]() {
          return Model(path);
      });
  }
  ```
- Use thread-safe queues for GPU upload requests
- Transfer queue for background uploads
- Show loading screen or placeholder while loading
- Prevents frame hitches during asset loads

#### 12.2 Parallel Command Recording
**Priority: Low**

**Files Affected:**
- `src/vk_engine.cpp`
- Command buffer management

**Action Items:**
- Record command buffers in parallel using secondary command buffers
- Requires thread-local command pools
- Split scene into chunks, record in parallel
- Measure if parallel recording provides benefit
- Modern drivers may parallelize internally already

#### 12.3 Async Compute
**Priority: Low** (Advanced)

**Files Affected:**
- Extensive changes
- Compute pipeline setup

**Action Items:**
- Use separate compute queue for async work:
  - Particle simulation
  - Post-processing effects
  - Culling and LOD selection
- Overlap compute with graphics
- Requires careful synchronization with timeline semaphores
- Profile to ensure compute doesn't starve graphics or vice versa

---

## 13. Ray Tracing (Future)

### Current State
- No ray tracing support
- Rasterization-only rendering

### Modernization Actions

#### 13.1 Ray Tracing Evaluation
**Priority: Low** (Future enhancement)

**Files Affected:**
- Would require extensive additions

**Action Items:**
- Evaluate VK_KHR_acceleration_structure and VK_KHR_ray_tracing_pipeline
- Potential uses:
  - Reflections (ray traced vs screen space)
  - Ambient occlusion
  - Global illumination
  - Shadows
- Start with hybrid (raster + RT) approach
- Requires significant GPU hardware support (NVIDIA RTX, AMD RDNA2+)
- Consider fallback path for older hardware

---

## 14. Content Pipeline Integration

### Current State
- Runtime asset loading (glTF, OBJ via Assimp)
- No asset preprocessing
- No custom binary format
- Shader compilation at build time

### Modernization Actions

#### 14.1 Asset Preprocessing Pipeline
**Priority: Medium**

**Files Affected:**
- Create offline asset processor
- Asset loading code

**Action Items:**
- Create offline tool to preprocess assets:
  - Convert to optimized binary format
  - Compress textures to GPU formats
  - Generate mipmaps offline
  - Optimize mesh data (cache optimization, compression)
  - Pre-bake acceleration structures
- Store preprocessed assets in custom format
- Dramatically faster loading at runtime
- Smaller file sizes

#### 14.2 Mesh Optimization
**Priority: Medium**

**Files Affected:**
- Asset pipeline
- `src/vk_loader.cpp`

**Action Items:**
- Integrate meshoptimizer library:
  - Vertex cache optimization
  - Overdraw reduction
  - Vertex fetch optimization
  - LOD generation
  - Mesh compression
- Apply during asset preprocessing
- Improves GPU efficiency

---

## Implementation Roadmap

### Phase 1: Foundation & Debugging (Weeks 1-2)
1. Enhanced debug markers throughout codebase
2. GPU profiling with timestamps
3. Better validation layer configuration
4. Statistics and instrumentation system

### Phase 2: Descriptor & Pipeline Modernization (Weeks 3-5)
1. Bindless texture system implementation
2. Pipeline caching with serialization
3. Expand dynamic state usage
4. Graphics pipeline library (if supported)

### Phase 3: Memory & Buffer Management (Weeks 6-7)
1. Unified buffer allocator
2. Optimize vertex/index buffer layout
3. Staging buffer pool
4. Memory budget tracking

### Phase 4: Synchronization & Command Buffers (Week 8)
1. Timeline semaphores
2. Fine-grained barriers
3. Command buffer abstraction
4. Remove unnecessary synchronization

### Phase 5: Advanced Rendering (Weeks 9-11)
1. GPU-driven rendering with indirect draw
2. Compressed texture support
3. Enhanced PBR with IBL
4. TAA or better anti-aliasing

### Phase 6: Async & Multi-Threading (Weeks 12-13)
1. Async asset loading
2. Transfer queue usage
3. Optional: Parallel command recording
4. Optional: Async compute

### Phase 7: Content Pipeline (Weeks 14-15)
1. Asset preprocessing tool
2. Mesh optimization integration
3. Custom binary format
4. Shader hot-reloading

### Phase 8: Polish & Optimization (Week 16+)
1. Profile and optimize based on telemetry
2. Hardware-specific optimizations
3. Feature detection and fallbacks
4. Documentation

---

## Performance Targets

| Metric | Current | Target | How to Measure |
|--------|---------|--------|----------------|
| Frame time (1080p) | ~2-5ms | <2ms | GPU timestamps |
| Draw call overhead | Unknown | <0.1ms per 1000 draws | GPU profiling |
| Descriptor updates | Unknown | <100 per frame | Validation layers |
| Memory usage | Unknown | <500MB for test scene | Memory budget API |
| Pipeline creation | ~100ms | <10ms (cached) | CPU timing |
| Asset load time | Seconds | <100ms (preprocessed) | CPU timing |

---

## Hardware Support Considerations

### Minimum Spec (Vulkan 1.3)
- Dynamic rendering
- Synchronization2
- Buffer device address
- Descriptor indexing

### Recommended Extensions
- VK_EXT_descriptor_buffer
- VK_EXT_graphics_pipeline_library
- VK_KHR_fragment_shading_rate
- VK_EXT_mesh_shader

### Optional (Future)
- VK_KHR_ray_tracing_pipeline
- VK_KHR_acceleration_structure

---

## Risks & Mitigation

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Feature not supported on target hardware | High | Medium | Implement feature detection and fallbacks |
| Performance regression from new techniques | High | Medium | Profile before and after, keep old path as option |
| Increased GPU memory usage | Medium | Medium | Implement streaming and LOD systems |
| Complexity increase | Medium | High | Modularize changes, comprehensive documentation |
| Driver bugs with new features | High | Low | Test on multiple vendors, report bugs upstream |

---

## Testing Strategy

### Validation
- Run with all validation layers enabled
- Test on multiple GPU vendors (NVIDIA, AMD, Intel)
- Verify with RenderDoc and PIX captures
- Test with different Vulkan implementations

### Performance
- Establish baseline metrics before changes
- Profile each major change with GPU timestamps
- Test on different hardware tiers
- Stress test with large scenes (10K+ objects)

### Compatibility
- Test on older hardware (Vulkan 1.0 fallback if needed)
- Verify on integrated GPUs
- Test on different OS (Windows, Linux)
- Validate swapchain handling with various display configs

---

## Useful Resources & Libraries

### Libraries
- **VMA** (Vulkan Memory Allocator) - Already integrated ✓
- **vk-bootstrap** - Already integrated ✓
- **meshoptimizer** - For mesh optimization
- **KTX-Software** - For texture compression
- **SPIRV-Reflect** - For shader reflection
- **shaderc** - Runtime shader compilation
- **volk** - Meta-loader for Vulkan

### Tools
- **RenderDoc** - Frame capture and debugging
- **NVIDIA Nsight Graphics** - Profiling and debugging
- **AMD Radeon GPU Profiler** - AMD-specific profiling
- **PIX** - Windows GPU debugging
- **Compressonator** - Texture compression

### References
- Vulkan Spec 1.3
- Vulkan Guide (vkguide.dev)
- GPU Gems / GPU Pro series
- ARM Best Practice Guide
- NVIDIA Vulkan Best Practices

---

## Conclusion

This modernization plan transforms the Vulkan rendering from a basic implementation to a high-performance, modern renderer utilizing Vulkan 1.3+ features. The phased approach allows for incremental improvements while maintaining stability. Priority focuses on bindless descriptors, pipeline caching, GPU-driven rendering, and proper memory management before moving to advanced features like ray tracing or mesh shaders.

Key themes:
- **Reduce CPU overhead**: Bindless, GPU culling, indirect draws, less state changes
- **Improve GPU utilization**: Better synchronization, async compute, parallel work
- **Faster iteration**: Hot-reloading, better debugging, profiling tools
- **Scalability**: Streaming, LOD, memory management, large scenes
- **Quality**: Better PBR, anti-aliasing, HDR support

The result will be a renderer that can handle complex scenes efficiently while providing excellent developer experience for iteration and debugging.



