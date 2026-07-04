# Phase 5: Break Up the VulkanEngine Monolith

> **Baseline commit:** `a808d4b`. All symbol names and member references are accurate as of this commit. If the code has drifted, use symbol search rather than the names below.

## Current State

`vk_engine.cpp` is **~1,680 lines** and `vk_engine.h` is **~293 lines**. The `VulkanEngine` class owns every responsibility in the application: window creation, Vulkan device setup, swapchain management, memory allocation, image/buffer creation, mesh uploading, pipeline compilation, descriptor management, ImGui integration, the main render loop, scene management, post-processing, and cleanup.

This makes it extremely difficult to:
- Find where a bug lives (is it a swapchain issue? a resource leak? a draw-order problem?)
- Add new features without reading the entire file
- Test or reuse any subsystem in isolation

## Goal

Split `VulkanEngine` into **three focused modules** so that each file has a single, clear responsibility. `VulkanEngine` itself becomes a thin ~200-line orchestrator that wires the subsystems together and runs the main loop.

---

## Before / After File Map

| Before | After | Approx lines | Responsibility |
|---|---|---|---|
| `src/vk_engine.h` (293 lines) | `src/vk_engine.h` | ~80 | Orchestrator: wires subsystems, runs main loop |
| `src/vk_engine.cpp` (1,680 lines) | `src/vk_engine.cpp` | ~200 | `init()`, `run()`, `cleanup()`, `handleImGui()`, `init_commands()`, `init_sync_structures()`, `init_renderables()` |
| _(new)_ | `src/vk_frame.h` | ~30 | `FrameGarbage`, `FrameData`, `FRAME_OVERLAP` — shared across all modules |
| _(new)_ | `src/vk_context.h` / `.cpp` | ~250 | Instance, device, queues, swapchain, surface |
| _(new)_ | `src/resource_manager.h` / `.cpp` | ~400 | VMA, buffers, images, mesh upload, deferred destruction |
| _(new)_ | `src/renderer.h` / `.cpp` | ~800 | Pipelines, descriptors, draw loop, post-processing |

---

## Header Dependency Graph

The include graph must be strictly acyclic. Each module only includes what it needs:

```
vk_types.h              (no project deps — Vulkan types, AllocatedImage/Buffer, RAII)
  ↑
vk_frame.h              (FrameGarbage, FrameData, FRAME_OVERLAP — depends on vk_types.h)
  ↑
vk_context.h            (depends on vk_types.h only)
  ↑
resource_manager.h      (depends on vk_types.h, vk_frame.h)
  ↑
renderer.h              (depends on vk_context.h, resource_manager.h, vk_frame.h)
  ↑
vk_engine.h             (depends on all above — the orchestrator)
```

**Rule:** No module may `#include "vk_engine.h"`. Forward-declare `VulkanEngine` where a pointer/reference is needed (e.g. `Editor::handleUi`). Violations mean a circular dependency.

---

## Proposed Architecture

```
VulkanEngine (orchestrator)
  ├── VulkanContext     — Device, queues, swapchain, surface
  ├── ResourceManager   — VMA, buffers, images, mesh upload, deferred destruction
  └── Renderer          — Pipelines, descriptors, draw loop, post-processing
```

---

### Shared Header: `vk_frame.h`

**Why this exists:** `FrameGarbage` and `FrameData` are cross-cutting — the orchestrator owns the array, `ResourceManager` writes into `FrameGarbage` via `defer_destroy`, and `Renderer` reads/writes `FrameData` fields during the draw loop. Putting them in their own header avoids circular includes and makes the sharing explicit.

```cpp
// src/vk_frame.h
#pragma once
#include <vk_types.h>
#include <vk_descriptors.h>

struct FrameGarbage {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkSampler> samplers;

    void flush(VkDevice device, VmaAllocator allocator);
};

struct FrameData {
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    uint64_t _frameTimelineValue {0};
    FrameGarbage _garbage;
    AllocatedBuffer sceneDataBuffer;
    VkDescriptorSet globalDescriptor;

    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;
};

constexpr unsigned int FRAME_OVERLAP = 2;
```

**Moved from:** `vk_engine.h` (lines 58–79).

---

### Module 1: `VulkanContext`

**Files:** `src/vk_context.h`, `src/vk_context.cpp`

**Owns:**
| Member | Current symbol in `VulkanEngine` |
|---|---|
| `VkInstance _instance` | `instance` |
| `VkDebugUtilsMessengerEXT _debugMessenger` | `debug_messenger` |
| `VkPhysicalDevice _chosenGPU` | `chosenGPU` |
| `VkDevice _device` | `device` |
| `VkQueue _graphicsQueue` | `graphicsQueue` |
| `uint32_t _graphicsQueueFamily` | `graphicsQueueFamily` |
| `VkSurfaceKHR _surface` | `surface` |
| `VkSwapchainKHR _swapchain` | `swapchain` |
| `VkFormat _swapchainImageFormat` | `swapchainImageFormat` |
| `VkExtent2D _swapchainExtent` | `swapchainExtent` |
| `std::vector<VkImage> _swapchainImages` | `swapchainImages` |
| `std::vector<VkImageView> _swapchainImageViews` | `swapchainImageViews` |
| `std::vector<VkSemaphore> _swapchainRenderSemaphores` | `swapchainRenderSemaphores` |
| `std::vector<bool> _swapchainImageInitialized` | `swapchainImageInitialized` |
| `VkExtent2D _windowExtent` | `windowExtent` |
| `struct SDL_Window* _window` | `window` |

**Methods moved from `VulkanEngine`:**
| Method | Source symbol |
|---|---|
| `init_vulkan()` | `VulkanEngine::init_vulkan` |
| `init_swapchain()` | `VulkanEngine::init_swapchain` |
| `create_swapchain()` | `VulkanEngine::create_swapchain` |
| `destroy_swapchain()` | `VulkanEngine::destroy_swapchain` |
| `resize_swapchain()` | `VulkanEngine::resize_swapchain` |

**Public interface (new):**
```cpp
class VulkanContext {
public:
    void init(SDL_Window* window);
    void cleanup();

    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();
    void resize_swapchain();

    // Accessors (all const refs or values)
    VkDevice device() const;
    VkPhysicalDevice gpu() const;
    VkInstance instance() const;
    VkQueue graphics_queue() const;
    uint32_t graphics_queue_family() const;
    VkSurfaceKHR surface() const;

    // Swapchain state
    VkSwapchainKHR swapchain() const;
    VkFormat swapchain_format() const;
    VkExtent2D swapchain_extent() const;
    std::span<VkImage> swapchain_images();
    std::span<VkImageView> swapchain_image_views();
    std::span<VkSemaphore> swapchain_render_semaphores();
    std::span<bool> swapchain_image_initialized();

    VkExtent2D window_extent() const;

private:
    // all members listed above
};
```

**Why this boundary:** Everything in this class is created once at startup and destroyed at shutdown (except the swapchain, which is rebuilt on resize). It has zero per-frame state and zero dependencies on rendering pipelines or resource allocation. It is pure Vulkan plumbing.

---

### Module 2: `ResourceManager`

**Files:** `src/resource_manager.h`, `src/resource_manager.cpp`

**Owns:**
| Member | Current symbol in `VulkanEngine` |
|---|---|
| `VmaAllocator _allocator` | `allocator` |
| `VkFence _immFence` | `immFence` |
| `VkCommandBuffer _immCommandBuffer` | `immCommandBuffer` |
| `VkCommandPool _immCommandPool` | `immCommandPool` |
| `AllocatedImage _whiteImage` | `whiteImage` |
| `AllocatedImage _blackImage` | `blackImage` |
| `AllocatedImage _greyImage` | `greyImage` |
| `AllocatedImage _errorCheckerboardImage` | `errorCheckerboardImage` |
| `VkSampler _defaultSamplerLinear` | `defaultSamplerLinear` |
| `VkSampler _defaultSamplerNearest` | `defaultSamplerNearest` |
| `AllocatedBuffer _defaultGLTFMaterialData` | `defaultGLTFMaterialData` |
| `GPUMeshBuffers _rectangle` | `rectangle` |

> **Note:** `rectangle` is created in `init_default_data()` and is a GPU resource — it belongs here, not in the orchestrator.

**Methods moved from `VulkanEngine`:**
| Method | Source symbol |
|---|---|
| `create_image()` (2 overloads) | `VulkanEngine::create_image` |
| `uploadMesh()` (2 overloads) | `VulkanEngine::uploadMesh` |
| `immediate_submit()` | `VulkanEngine::immediate_submit` |
| `defer_destroy()` (3 overloads) | `VulkanEngine::defer_destroy` |
| `init_default_data()` | `VulkanEngine::init_default_data` |
| `sendModelDataToGpu()` | `VulkanEngine::sendModelDataToGpu` |

**Also absorbs the free functions:**
| Function | Current location |
|---|---|
| `vkutil::defer_destroy_buffer()` | `vk_engine.cpp` (namespace `vkutil`) |
| `vkutil::defer_destroy_image()` | `vk_engine.cpp` (namespace `vkutil`) |
| `vkutil::createBuffer()` | `shared/vk_initializers.cpp` |
| `FrameGarbage::flush()` | `vk_engine.cpp` |

#### Deferred Destruction: How `ResourceManager` Accesses Frame Garbage

The current `defer_destroy()` methods push into `get_current_frame()._garbage`, which lives in `VulkanEngine::frames[]`. After the split, `ResourceManager` does **not** own the frame array.

**Chosen approach:** `ResourceManager` stores a `FrameData*` pointer and a `const int*` to `frameNumber` (or a `std::function<FrameData&()>` callback), set during `init()`. This lets `defer_destroy()` write into the correct frame's garbage bin without owning the frame array.

```cpp
// In ResourceManager::init():
void init(VulkanContext& ctx, FrameData* frames, const int* frameNumberPtr);

// In ResourceManager::defer_destroy():
void defer_destroy(VkBuffer buffer, VmaAllocation allocation) {
    current_frame()._garbage.buffers.push_back(buffer);
    current_frame()._garbage.allocations.push_back(allocation);
}

// Private helper:
FrameData& current_frame() { return _frames[*_frameNumber % FRAME_OVERLAP]; }
```

The `vkutil::defer_destroy_*` free functions in `vk_types.h` (used by RAII destructors in `AllocatedImage` / `AllocatedBuffer`) currently route through `VulkanEngine::Get()`. After the split, they route through `ResourceManager::Get()` instead — same singleton pattern, different class.

**Public interface (new):**
```cpp
class ResourceManager {
public:
    static ResourceManager& Get();

    void init(VulkanContext& ctx, FrameData* frames, const int* frameNumber);
    void cleanup(VkDevice device);

    // Image creation
    AllocatedImage create_image(void* data, VkExtent3D size,
                                VkFormat format, VkImageUsageFlags usage,
                                bool mipmapped = false);
    AllocatedImage create_image(VkCommandBuffer cmd,
                                std::vector<AllocatedBuffer>& stagingBuffers,
                                void* data, VkExtent3D size,
                                VkFormat format, VkImageUsageFlags usage,
                                bool mipmapped = false);

    // Mesh upload
    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices,
                              std::span<Vertex> vertices);
    GPUMeshBuffers uploadMesh(VkCommandBuffer cmd,
                              std::vector<AllocatedBuffer>& stagingBuffers,
                              std::span<uint32_t> indices,
                              std::span<Vertex> vertices);

    // Blocking one-shot GPU work
    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

    // Model data upload
    void sendModelDataToGpu(Model& model);

    // Per-frame garbage collection
    void defer_destroy(VkBuffer buffer, VmaAllocation allocation);
    void defer_destroy(VkImage image, VkImageView view, VmaAllocation allocation);
    void defer_destroy(VkSampler sampler);

    // Accessors for default resources
    VmaAllocator vma() const;
    VkSampler sampler_linear() const;
    VkSampler sampler_nearest() const;
    const AllocatedImage& white_image() const;
    const AllocatedImage& black_image() const;
    const AllocatedImage& grey_image() const;
    const AllocatedImage& error_image() const;
    const GPUMeshBuffers& rectangle() const;

private:
    FrameData* _frames = nullptr;
    const int* _frameNumber = nullptr;

    FrameData& current_frame();

    // all other members listed above
};
```

**Why this boundary:** Every function here is about allocating, uploading, or destroying GPU memory. None of them know about pipelines, descriptors, or draw calls. The deferred destruction system (`FrameGarbage`) is tightly coupled to the allocator — it belongs here, not in the renderer.

---

### Module 3: `Renderer`

**Files:** `src/renderer.h`, `src/renderer.cpp`

**Owns:**
| Member | Current symbol in `VulkanEngine` |
|---|---|
| `AllocatedImage _drawImage` | `drawImage` |
| `AllocatedImage _depthImage` | `depthImage` |
| `AllocatedImage _postProcessingImage` | `postProcessingImage` |
| `VkPipeline _postProcessPipeline` | `postProcessPipeline` |
| `VkPipelineLayout _postProcessPipelineLayout` | `postProcessPipelineLayout` |
| `VkDescriptorSetLayout _postProcessImageLayout` | `postProcessImageLayout` |
| `VkDescriptorSet _postProcessDescriptorSet` | `postProcessDescriptorSet` |
| `VkPipeline _gradientPipeline` | `gradientPipeline` |
| `VkPipelineLayout _gradientPipelineLayout` | `gradientPipelineLayout` |
| `std::vector<ComputeEffect> _backgroundEffects` | `backgroundEffects` |
| `int _currentBackgroundEffect` | `currentBackgroundEffect` |
| `DescriptorAllocator _globalDescriptorAllocator` | `globalDescriptorAllocator` |
| `VkDescriptorSet _drawImageDescriptors` | `drawImageDescriptors` |
| `VkDescriptorSetLayout _drawImageDescriptorLayout` | `drawImageDescriptorLayout` |
| `VkDescriptorSetLayout _gpuSceneDataDescriptorLayout` | `gpuSceneDataDescriptorLayout` |
| `uint32_t _globalTextureCount` | `globalTextureCount` |
| `GLTFMetallic_Roughness _metalRoughMaterial` | `metalRoughMaterial` |
| `VkDescriptorPool _imguiPool` | `imguiPool` |
| `GPUSceneData _sceneData` | `sceneData` |
| `DrawContext _drawCommands` | `drawCommands` |
| `EngineStats _stats` | `stats` |
| `VkExtent2D _drawExtent` | `drawExtent` |
| `float _renderScale` | `renderScale` |

> **Note:** `VkDescriptorPool descriptorPool` (line 155 in `vk_engine.h`) is unused dead code — delete it during the extraction. Do not move it.

**Methods moved from `VulkanEngine`:**
| Method | Source symbol |
|---|---|
| `draw()` | `VulkanEngine::draw` |
| `draw_main()` | `VulkanEngine::draw_main` |
| `draw_geometry()` | `VulkanEngine::draw_geometry` |
| `draw_imgui()` | `VulkanEngine::draw_imgui` |
| `render_nodes()` | `VulkanEngine::render_nodes` |
| `init_pipelines()` | `VulkanEngine::init_pipelines` |
| `init_background_pipelines()` | `VulkanEngine::init_background_pipelines` |
| `init_post_process_pipeline()` | `VulkanEngine::init_post_process_pipeline` |
| `init_descriptors()` | `VulkanEngine::init_descriptors` |
| `init_imgui()` | `VulkanEngine::init_imgui` |
| `add_global_texture()` | `VulkanEngine::add_global_texture` |
| `is_visible()` (free fn) | `vk_engine.cpp` |

**Also absorbs these types (move from `vk_engine.h` to `renderer.h`):**
| Type | Current location |
|---|---|
| `GLTFMetallic_Roughness` (struct + methods) | `vk_engine.h`, `vk_engine.cpp` |
| `ComputeEffect`, `ComputePushConstants` | `vk_engine.h` |
| `RenderObject`, `DrawContext`, `EngineStats` | `vk_engine.h` |
| `MeshNode` | `vk_engine.h` |

#### `add_global_texture` Cross-Cutting Dependency

`add_global_texture()` writes into `frames[i].globalDescriptor` for every frame in flight. Since `frames[]` stays in `VulkanEngine`, the Renderer needs access to the frame array for this operation.

**Chosen approach:** `Renderer::init()` receives a `std::span<FrameData, FRAME_OVERLAP>` (or `FrameData*` + count) and stores it. `add_global_texture()` uses the stored span to update every frame's descriptor set.

```cpp
// In Renderer::init():
void init(VulkanContext& ctx, ResourceManager& resources,
          std::span<FrameData, FRAME_OVERLAP> frames);

// In Renderer::add_global_texture():
int add_global_texture(VkDevice device, VkImageView view, VkSampler sampler) {
    int index = _globalTextureCount++;
    // ... build VkWriteDescriptorSet ...
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        writes[i].dstSet = _frames[i].globalDescriptor;
    }
    vkUpdateDescriptorSets(device, FRAME_OVERLAP, writes, 0, nullptr);
    return index;
}
```

#### `update_scene` Uses `loadedScenes` From the Orchestrator

`update_scene()` currently calls `loadedScenes["structure"]->Draw(...)` to populate `drawCommands`. Since `loadedScenes` stays in `VulkanEngine`, this creates a coupling.

**Chosen approach:** Split the method. The projection/view math and `drawCommands` population stay in `Renderer::update_scene()`, but the orchestrator passes in the draw commands by calling scene `Draw()` before invoking the renderer:

```cpp
// In VulkanEngine::run():
renderer.draw_commands().OpaqueSurfaces.clear();
renderer.draw_commands().TransparentSurfaces.clear();
loadedScenes["structure"]->Draw(glm::mat4{1.f}, renderer.draw_commands());
// ... imported models ...
renderer.update_scene(mainCamera.getViewMatrix(), context.window_extent());
```

This way `Renderer::update_scene()` only does camera math — it never touches scene storage.

#### `GLTFMetallic_Roughness::build_pipelines` Signature Change

`build_pipelines(VulkanEngine* engine)` reaches into the engine for device, descriptor layouts, and render state. After the split, change the signature to:

```cpp
void build_pipelines(VulkanContext& ctx, Renderer& renderer);
```

Or, if you prefer not coupling the material struct to the Renderer class, pass the individual values it needs:

```cpp
void build_pipelines(VkDevice device,
                     VkDescriptorSetLayout sceneDataLayout,
                     VkDescriptorSetLayout drawImageLayout,
                     VkFormat drawFormat, VkFormat depthFormat);
```

The second form is more verbose but keeps `GLTFMetallic_Roughness` independent of the Renderer header.

**Public interface (new):**
```cpp
class Renderer {
public:
    void init(VulkanContext& ctx, ResourceManager& resources,
              std::span<FrameData, FRAME_OVERLAP> frames);
    void cleanup(VkDevice device);

    void draw(VulkanContext& ctx, ResourceManager& resources,
              FrameData& frame, Camera& camera);

    void update_scene(const glm::mat4& viewMatrix, VkExtent2D windowExtent);

    // Pipeline access for material system
    GLTFMetallic_Roughness& material_system();
    DescriptorAllocator& descriptor_allocator();
    VkDescriptorSetLayout scene_data_layout() const;

    int add_global_texture(VkDevice device, VkImageView view, VkSampler sampler);

    // Stats & state (accessed by orchestrator for ImGui)
    const EngineStats& stats() const;
    float& render_scale();
    int& current_background_effect();
    std::vector<ComputeEffect>& background_effects();
    DrawContext& draw_commands();
    GPUSceneData& scene_data();

private:
    std::span<FrameData, FRAME_OVERLAP> _frames;

    // all members listed above
    // all init_* and draw_* methods become private
};
```

**Why this boundary:** Everything here is about *what to draw and how*. It depends on `VulkanContext` for the device/swapchain and on `ResourceManager` for image/buffer creation, but it never creates a device or manages raw memory itself.

---

### What stays in `VulkanEngine`

After the extraction, `VulkanEngine` becomes a ~200-line orchestrator:

```cpp
class VulkanEngine {
public:
    // Subsystems
    VulkanContext context;
    ResourceManager resources;
    Renderer renderer;

    // Per-frame data (shared across subsystems)
    FrameData frames[FRAME_OVERLAP];
    VkSemaphore _timelineSemaphore;
    uint64_t _timelineValue;
    int frameNumber;

    // Application state
    Camera mainCamera;
    bool resize_requested;
    bool freeze_rendering;

    // Scene data (owns loaded models/scenes)
    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;
    std::vector<std::shared_ptr<LoadedGLTF>> brickadiaScene;
    std::vector<std::unique_ptr<Model>> importedModels;

    static VulkanEngine& Get();

    void init();
    void run();
    void cleanup();

    FrameData& get_current_frame();
    FrameData& get_last_frame();

private:
    void init_commands();        // creates per-frame command pools/buffers ONLY (see note below)
    void init_sync_structures(); // creates timeline semaphore + per-frame semaphores ONLY
    void init_renderables();     // loads scenes and models
    void handleImGui();          // ImGui frame: Editor UI + debug panels (reads from renderer)
};
```

#### `init_commands` and `init_sync_structures` must be split

These two functions currently create resources that belong to **different modules** after the split:

**`init_commands()` today creates:**
- Per-frame `commandPool` + `commandBuffer` → stays in `VulkanEngine::init_commands()`
- `immCommandPool` + `immCommandBuffer` → moves to `ResourceManager::init()` (it owns the immediate-submit infrastructure)

**`init_sync_structures()` today creates:**
- `immFence` → moves to `ResourceManager::init()`
- `_timelineSemaphore` + per-frame semaphores → stays in `VulkanEngine::init_sync_structures()`

After the split, the init call order in `VulkanEngine::init()` becomes:
```cpp
void VulkanEngine::init() {
    loadedEngine = this;
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(...);

    context.init(window);                                  // Step 1: device, swapchain
    init_commands();                                        // per-frame cmd pools only
    init_sync_structures();                                 // timeline + per-frame semaphores only
    resources.init(context, frames, &frameNumber);          // Step 2: VMA, imm cmd pool/fence, default data
    renderer.init(context, resources, frames);              // Step 3: descriptors, pipelines, imgui
    init_renderables();                                     // loads scenes via loadGltf
}
```

> **Note:** `init_commands()` and `init_sync_structures()` must run before `resources.init()` because per-frame command pools are needed for frame setup, and `resources.init()` creates the immediate-submit pool/fence internally.

#### `handleImGui()` stays in the orchestrator

`handleImGui()` reads from multiple subsystems (`renderer.stats()`, `renderer.background_effects()`, `renderer.scene_data()`) and calls `Editor::handleUi(this)`, which itself calls `sendModelDataToGpu()`. It is naturally an orchestration concern — wiring UI controls to subsystem state. It stays in `VulkanEngine` and accesses renderer state through the public accessors defined above.

After the split, `handleImGui()` looks like:

```cpp
void VulkanEngine::handleImGui() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    Editor::handleUi(this);  // still takes VulkanEngine* for model loading

    ImGui::Begin("Stats");
    ImGui::Text("frametime %f ms", renderer.stats().frametime);
    ImGui::Text("drawtime %f ms", renderer.stats().mesh_draw_time);
    // ...
    ImGui::End();

    ImGui::Begin("background");
    ComputeEffect& selected = renderer.background_effects()[renderer.current_background_effect()];
    // ...
    ImGui::SliderFloat4("Sunlight Color", (float*)&renderer.scene_data().sunlightColor, 0.0f, 1.0f);
    // ...
    ImGui::End();
    ImGui::Render();
}
```

#### `Editor::handleUi` signature

`Editor::handleUi(VulkanEngine* engine)` calls `engine->sendModelDataToGpu(model)`. After the split, `sendModelDataToGpu` lives on `ResourceManager`. Two options:

1. **Keep `VulkanEngine*`** — `Editor::handleUi` calls `engine->resources.sendModelDataToGpu(model)`. Simplest; no signature change.
2. **Change to `ResourceManager&`** — cleaner but changes the `Editor` API.

**Recommended:** Option 1. The Editor is application-level code that may need more engine state in the future.

---

## Implementation Order

The extraction must be done in dependency order to keep the build green at every step. **Each step is a single commit** so `git revert` is the rollback strategy if a step breaks.

### Step 0: Extract `vk_frame.h` (zero-risk prep)
1. Create `src/vk_frame.h`
2. Move `FrameGarbage`, `FrameData`, and `FRAME_OVERLAP` from `vk_engine.h`
3. `#include "vk_frame.h"` in `vk_engine.h`
4. **Build.** This is a pure header shuffle — no behavior change.

### Step 1: Extract `VulkanContext` (lowest dependency)
1. Create `src/vk_context.h` and `src/vk_context.cpp`
2. Move all Vulkan instance/device/swapchain members and methods
3. Add `VulkanContext context;` to `VulkanEngine`
4. Replace all `device` → `context.device()`, `swapchain` → `context.swapchain()`, etc. throughout `vk_engine.cpp`
5. Update `src/CMakeLists.txt` to compile the new file
6. **Build. Run. Load a scene. Confirm no validation errors. Confirm identical rendering.**

### Step 2: Extract `ResourceManager` (depends on VulkanContext)
1. Create `src/resource_manager.h` and `src/resource_manager.cpp`
2. Move VMA allocator, immediate submit, image/buffer creation, deferred destruction
3. Move default textures, samplers, and `rectangle`
4. Wire up `ResourceManager::init()` with `FrameData*` and `frameNumber*` pointers (see "Deferred Destruction" section above)
5. Update the `vkutil::defer_destroy_*` free functions in `vk_engine.cpp` to route through `ResourceManager::Get()` instead of `VulkanEngine::Get()`
6. Add `static ResourceManager& Get()` singleton accessor
7. Update `vk_loader.cpp`: change `LoadedGLTF::creator` from `VulkanEngine*` to `ResourceManager*` (see "vk_loader.cpp Rewiring" section below)
8. Update `src/CMakeLists.txt`
9. **Build. Run. Load a scene. Confirm no validation errors. Confirm identical rendering.**

### Step 3: Extract `Renderer` (depends on both)
1. Create `src/renderer.h` and `src/renderer.cpp`
2. Move all pipeline, descriptor, draw, and post-processing logic
3. Move `GLTFMetallic_Roughness`, `ComputeEffect`, `DrawContext`, `RenderObject`, `MeshNode` to `renderer.h`
4. Change `GLTFMetallic_Roughness::build_pipelines` signature (see section above)
5. Move `init_imgui()` and `draw_imgui()` — these are rendering concerns
6. Wire up `Renderer::init()` with the `FrameData` span (see `add_global_texture` section above)
7. Update `vk_loader.cpp`: `loadGltf()` now takes `ResourceManager&` + `Renderer&` (see below)
8. Update `src/CMakeLists.txt`
9. **Build. Run. Load a scene. Confirm no validation errors. Confirm identical rendering.**

### Step 4: Slim down `VulkanEngine`
1. Remove all moved members and methods from `vk_engine.h`
2. Delete the dead `VkDescriptorPool descriptorPool` member
3. Split `init_commands()`: move `immCommandPool`/`immCommandBuffer` creation into `ResourceManager::init()`
4. Split `init_sync_structures()`: move `immFence` creation into `ResourceManager::init()`
5. `VulkanEngine::init()` becomes the sequence shown above in "init_commands and init_sync_structures must be split"
6. `VulkanEngine::cleanup()` becomes the explicit sequence shown below
7. `VulkanEngine::run()` delegates resize to `context.resize_swapchain()`, builds draw commands from `loadedScenes`, then calls `renderer.draw(...)`
8. `handleImGui()` reads renderer state through accessors (see above)
9. **Final build. Run. Load a scene. Confirm no validation errors. Confirm identical rendering.**

#### Post-split `cleanup()` sequence

The current `cleanup()` interleaves renderer, resource, and context teardown. After the split, the order matters because RAII destructors in `AllocatedImage`/`AllocatedBuffer` call `defer_destroy` which writes into frame garbage — that garbage must be flushed before VMA is destroyed.

```cpp
void VulkanEngine::cleanup() {
    vkDeviceWaitIdle(context.device());

    // 1. Clear scene data — RAII destructors fire, pushing into frame garbage
    importedModels.clear();
    loadedScenes.clear();
    brickadiaScene.clear();

    // 2. Renderer cleanup — destroys draw/depth/post-process images (RAII → defer_destroy),
    //    pipelines, descriptors, descriptor pools, imgui
    renderer.cleanup(context.device());

    // 3. Resource cleanup — flushes ALL frame garbage, destroys default textures/samplers,
    //    imm cmd pool/fence, then destroys VMA allocator
    resources.cleanup(context.device());

    // 4. Destroy per-frame command pools and semaphores (owned by orchestrator)
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        vkDestroyCommandPool(context.device(), frames[i]._commandPool, nullptr);
        vkDestroySemaphore(context.device(), frames[i]._swapchainSemaphore, nullptr);
        vkDestroySemaphore(context.device(), frames[i]._renderSemaphore, nullptr);
    }
    vkDestroySemaphore(context.device(), _timelineSemaphore, nullptr);

    // 5. Context cleanup — swapchain, surface, device, instance, SDL window
    context.cleanup();
}
```

> **Critical:** `resources.cleanup()` MUST flush all frame garbage bins before destroying the VMA allocator. Any `AllocatedImage`/`AllocatedBuffer` whose RAII destructor fires after VMA is gone will crash. The ordering above guarantees this.

---

## `vk_loader.cpp` Rewiring (Detailed)

This is the largest callsite change and deserves its own section. The current coupling points:

| Usage in `vk_loader.cpp` | Routes to after split |
|---|---|
| `engine->create_image(cmd, ...)` | `resources.create_image(cmd, ...)` |
| `engine->uploadMesh(cmd, ...)` | `resources.uploadMesh(cmd, ...)` |
| `engine->immCommandBuffer` | `resources.imm_command_buffer()` |
| `engine->immFence` | `resources.imm_fence()` |
| `engine->device` | `resources.device()` (or pass `VulkanContext&`) |
| `engine->allocator` | `resources.vma()` |
| `engine->graphicsQueue` | passed via `ResourceManager` (stores from init) |
| `engine->errorCheckerboardImage` | `resources.error_image()` |
| `engine->whiteImage` | `resources.white_image()` |
| `engine->defaultSamplerLinear` | `resources.sampler_linear()` |
| `engine->add_global_texture(...)` | `renderer.add_global_texture(...)` |
| `engine->metalRoughMaterial.*` | `renderer.material_system().*` |

### `LoadedGLTF::creator` Change

`LoadedGLTF` stores `VulkanEngine* creator` and uses it in `clearAll()` to call `creator->defer_destroy(sampler)`. After the split:

```cpp
// vk_loader.h
struct LoadedGLTF : public IRenderable {
    // ...
    ResourceManager* creator;  // was VulkanEngine*
    // ...
};
```

`clearAll()` becomes:
```cpp
void LoadedGLTF::clearAll() {
    for (auto& sampler : samplers) {
        creator->defer_destroy(sampler);
    }
}
```

### `loadGltf()` Signature Change

```cpp
// Before:
std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine, std::string_view filePath);

// After:
std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(ResourceManager& resources, Renderer& renderer, std::string_view filePath);
```

---

## Risk Assessment

| Risk | Mitigation |
|---|---|
| **`FrameData` is cross-cutting** — sync primitives, garbage bins, scene descriptors, command buffers. Multiple modules need access. | Extract to `vk_frame.h`. Pass `FrameData&` or `std::span<FrameData>` to methods that need it. Don't try to split it. |
| **`defer_destroy` needs the current frame** — `ResourceManager` doesn't own the frame array but must write into `get_current_frame()._garbage`. | `ResourceManager` stores `FrameData*` + `const int*` to `frameNumber`, set during `init()`. See "Deferred Destruction" section. |
| **`add_global_texture` writes all frames' descriptors** — `Renderer` doesn't own `frames[]` but must update `globalDescriptor` on every frame. | `Renderer` stores a `std::span<FrameData, FRAME_OVERLAP>` set during `init()`. |
| **`update_scene` uses `loadedScenes`** — scene storage stays in the orchestrator but `update_scene` populates draw commands from it. | Orchestrator calls `Draw()` on scenes to populate `renderer.draw_commands()` before calling `renderer.update_scene()`. |
| **`vk_loader.cpp` takes `VulkanEngine*`** — tightly coupled to the monolith for uploads, material creation, and global texture registration. | Change `loadGltf()` to accept `ResourceManager&` + `Renderer&`. Change `LoadedGLTF::creator` to `ResourceManager*`. See detailed section above. |
| **`GLTFMetallic_Roughness::build_pipelines(VulkanEngine*)`** — material pipeline creation reaches into the engine. | Change signature to take `VulkanContext&` + `Renderer&` (or individual values). See section above. |
| **`Editor::handleUi(VulkanEngine*)`** calls `engine->sendModelDataToGpu()` — couples the editor to the monolith. | Keep `VulkanEngine*` parameter. The call becomes `engine->resources.sendModelDataToGpu(model)`. No editor API change needed. |
| **RAII destructors in `vk_types.h`** use `vkutil::defer_destroy_*` free functions that currently route through `VulkanEngine::Get()`. | Route through `ResourceManager::Get()` instead. Same singleton pattern, different class. |
| **Circular headers** — `Renderer` needs `VulkanContext` and `ResourceManager`; if any of them include `vk_engine.h`, we get cycles. | Strict include hierarchy (see Header Dependency Graph). No module may `#include "vk_engine.h"`. Forward-declare `VulkanEngine` where a pointer is needed. |
| **Shutdown order matters** — RAII destructors in moved `AllocatedImage`/`AllocatedBuffer` members fire during module destruction. If `ResourceManager` (which owns the VMA allocator) is destroyed before `Renderer` (which owns draw/depth images), the deferred destroys will reference a dead allocator. | Destroy in reverse init order: `renderer.cleanup()` → `resources.cleanup()` → `context.cleanup()`. Call explicit `cleanup()` before any destructor runs. |
| **`FrameGarbage::flush` needs both `VkDevice` and `VmaAllocator`** — after the split, `flush()` is called during frame sync, which happens in the draw loop (owned by Renderer) but also during final cleanup (owned by the orchestrator). | `flush()` takes `VkDevice` + `VmaAllocator` as parameters (already does today). Callers get these from their stored context/resources references. |

---

## Success Criteria

- `vk_engine.cpp` is under 250 lines
- Each new module compiles independently (no circular includes)
- The application renders identically before and after
- Zero validation errors on startup, rendering, and shutdown
- All existing functionality (ImGui, model loading, post-processing, background effects) works unchanged
- `git log --oneline` shows 4–5 clean commits (one per step), each individually revertible

---

## Quick Reference (Post-Refactor)

After the refactor, use this to find things:

| "Where is...?" | File |
|---|---|
| Vulkan instance / device / swapchain setup | `vk_context.cpp` |
| Image/buffer creation, mesh upload | `resource_manager.cpp` |
| Deferred destruction, VMA allocator | `resource_manager.cpp` |
| Default textures and samplers | `resource_manager.cpp` |
| Pipeline creation and compilation | `renderer.cpp` |
| Descriptor setup and allocation | `renderer.cpp` |
| Draw loop, geometry pass, post-processing | `renderer.cpp` |
| ImGui initialization and rendering | `renderer.cpp` (`init_imgui`, `draw_imgui`) |
| ImGui frame logic (stats panel, background controls) | `vk_engine.cpp` (`handleImGui`) |
| Scene loading, model management | `vk_engine.cpp` + `vk_loader.cpp` |
| Frame sync (timeline semaphores, command pools) | `vk_engine.cpp` |
| `FrameData`, `FrameGarbage`, `FRAME_OVERLAP` | `vk_frame.h` |
| Material types, render objects, compute effects | `renderer.h` |
