# Improvement ideas for vk-playground

This review of `master` at `c0d5d6f` covers three goals: making the app faster, making the code easier for an outside reader to follow, and adding technically impressive GPU techniques. Line references point to that commit.

The other docs in this folder already cover some of this ground: [ADVANCED_PROJECT_IDEAS.md](ADVANCED_PROJECT_IDEAS.md), [VULKAN_MODERNIZATION_PLAN.md](VULKAN_MODERNIZATION_PLAN.md) and [PHASE5_MONOLITH_BREAKUP.md](PHASE5_MONOLITH_BREAKUP.md). Items those docs already describe are linked here rather than repeated.

## A. Performance

Items are ordered by payoff relative to effort.

### A1. Let the CPU and GPU overlap frames (one-line fix)

**Problem:** `draw()` (`src/vk_engine.cpp:399-409`) waits until the timeline semaphore reaches `_timelineValue`. That is the value of the most recent submit, so the CPU waits for the previous frame to finish before it records the next one. `FRAME_OVERLAP = 2` behaves like 1, and the CPU and GPU never work at the same time.

**Change:** wait for `get_current_frame()._frameTimelineValue`, the value this frame slot signaled two frames ago. Skip the wait when that value is 0. `resize_swapchain()` should keep waiting for the latest value, because it needs the GPU to be idle.

**Expected gain:** frame time drops toward max(CPU, GPU) instead of CPU + GPU.

### A2. Enable validation layers only in debug builds

**Problem:** `bUseValidationLayers` is hard-coded to `true` (`vk_engine.cpp:27`), and best-practices and synchronization validation are turned on with it. Together they add a large CPU cost to every frame, including release builds.

**Change:** gate the flag on `NDEBUG`. This is [modernization plan §5.4](VULKAN_MODERNIZATION_PLAN.md), which is still open.

### A3. Batch uploads for imported models

**Problem:** `sendModelDataToGpu` (`vk_engine.cpp:1556`) calls `uploadMesh` once per mesh and `create_image` once per texture. Each call runs `immediate_submit` and waits on a fence, so a model with N meshes and M textures stalls the GPU N + M times.

**Change:** record every upload into one command buffer and submit it once, as `loadGltf` already does through the overloads that take a `VkCommandBuffer` argument. See also C6.

### A4. Generate mipmaps for glTF textures

**Problem:** `load_image` passes `mipmapped = false` (`vk_loader.cpp`, all three branches), but the glTF samplers request mip filtering with `maxLod = VK_LOD_CLAMP_NONE`. Distant surfaces sample full-resolution textures, which thrashes the texture cache and causes shimmering.

**Change:** pass `true`. `vkutil::generate_mipmaps` already exists.

### A5. Deduplicate and bounds-check the bindless texture array

**Problem:** Every material without a base color or metal-rough texture calls `add_global_texture(whiteImage, defaultSamplerLinear)` and takes a new slot (`vk_loader.cpp`, material loop). Nothing checks the 1000-descriptor limit set at `vk_engine.cpp:1358`, so a scene with about 500 untextured materials writes past the array.

**Change:** reserve fixed slots for the default textures at startup. Look up other slots in a `(VkImageView, VkSampler) → index` map, and assert on overflow.

### A6. Use one shared vertex buffer and one shared index buffer

**Problem:** Every glTF mesh gets its own vertex and index buffers, so `draw_geometry` calls `vkCmdBindIndexBuffer` every time the mesh changes.

**Change:** suballocate all meshes from one large vertex buffer and one large index buffer, and store offsets in `GeoSurface`. The index buffer is then bound once per frame. This is also a prerequisite for the indirect-draw work in [ADVANCED §2](ADVANCED_PROJECT_IDEAS.md#2-gpu-driven-rendering-and-mesh-lod).

### A7. Size the background dispatch with `drawExtent`

**Problem:** The background compute pass dispatches `windowExtent / 16` groups (`vk_engine.cpp:307`). When `renderScale` is below 1, or the window has grown past the size the draw image was created at, it launches threads for pixels that are never shown. Both shaders bounds-check, so this wastes work but doesn't write out of bounds.

**Change:** use `drawExtent`, which the post-process dispatch already does.

### A8. Use current VMA memory usages and add a present-mode toggle

- **VMA usage flags:** `VMA_MEMORY_USAGE_GPU_ONLY`, `CPU_TO_GPU` and `CPU_ONLY` are deprecated. Use `VMA_MEMORY_USAGE_AUTO` with `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` for upload buffers. VMA can then place per-frame data in device-local, host-visible memory on GPUs with resizable BAR.
- **Present mode:** the swapchain always uses `FIFO` (`vk_engine.cpp:1132`). Add a UI toggle for `MAILBOX` and `IMMEDIATE` so frame-time measurements aren't capped by vsync.

### A9. Fix the "drawtime" label

**Problem:** `stats.mesh_draw_time` (`vk_engine.cpp:321-327`) measures how long the CPU takes to record commands, not GPU time. The current label suggests GPU time.

**Change:** rename it to "record time". Add real GPU pass timings with timestamp queries, as described in [ADVANCED §5](ADVANCED_PROJECT_IDEAS.md#5-gpu-profiling-and-regression-scenes).

## B. Correctness bugs

These are cheap to fix. Each one also makes the code harder to trust for a new reader.

| Bug | Where | Effect |
|---|---|---|
| The post-process shader is loaded as `"post_process_comp"`, but CMake builds `post_process.comp.spv`. | `vk_engine.cpp:1534` | The app loads an old, hand-built `post_process_comp.spv`, so edits to the shader do nothing. On a clean checkout the load fails; the error is printed, but the pipeline is still created from an uninitialized `VkShaderModule`. |
| Base-color textures are uploaded as `VK_FORMAT_R8G8B8A8_UNORM`. | `vk_loader.cpp` `load_image`, `vk_engine.cpp:1565` | The sRGB-encoded data is treated as linear, and the new gamma pass then encodes it again, so colors look washed out. Use `_SRGB` for base color and keep `_UNORM` for data textures such as metal-rough and normal maps. |
| Texture width and height are swapped. | `vk_engine.cpp:1563` | Non-square textures from imported models are corrupted. |
| `drawCommands.OpaqueSurfaces[0]` is read without a check. | `vk_engine.cpp:713` | Crashes when no opaque surface is visible. |
| Imported models push `materialBuffer = 0`. | `vk_engine.cpp:723-726` | The fragment shader in `basic.slang` reads `PushConstants.material[0]` from a null device address. |
| The push-constant call uses stage `VERTEX` only, but the layout declares `VERTEX \| FRAGMENT`. | `vk_engine.cpp:726` | Triggers a validation error. |
| `ImGui::Text("Selected effect: ", selected.name)` | `vk_engine.cpp:1595` | The `%s` is missing, so the effect name never shows. |

## C. Clarity for an outside audience

### C1. Add a README

The repo has no `README.md`. Add one with:

- A screenshot.
- What the project demonstrates.
- Build steps: vcpkg, and `slangc` on `PATH`.
- Camera and UI controls.
- A diagram of the frame:

```mermaid
flowchart LR
    A[Background compute<br/>gradient / sky] --> B[Geometry pass<br/>dynamic rendering, bindless]
    B --> C[Tone map + gamma<br/>compute]
    C --> D[Blit to swapchain]
    D --> E[ImGui]
    E --> F[Present]
```

### C2. Delete dead code

- **Shaders:** only `basic`, `gradient_color_comp`, `sky_comp` and `post_process_comp` are loaded. The other 17 `.slang` files are tutorial leftovers (`triangle`, `funky_triangle`, `tri_mesh_*`, `colored_triangle*`, `mesh`, `mesh_pbr`, `textured_lit`, and so on). `scene_data.slang` and `pbr_header.slang` also declare `SceneData` at a different binding than `input_structures.slang` does. Delete them, or move anything worth keeping, such as the SH irradiance code, into a shared module.
- **Unused members in `vk_engine.h`:** `rectangle`, `gradientPipeline`, `descriptorPool`, `brickadiaScene` and `FrameData::_renderSemaphore`. Per-image `swapchainRenderSemaphores` replaced `_renderSemaphore`.
- **Bindless leftovers:** `GLTFMetallic_Roughness::MaterialConstants`, `MaterialResources` and `write_material` are unused since the switch to bindless textures and buffer device addresses (BDA). The loader builds `MaterialInstance` directly.
- **Other leftovers:** `#define CHAPTER_STAGE`, `using namespace std`, and the unused locals at `vk_engine.cpp:289-291`.

### C3. Remove tutorial scaffolding

The `//> intro` / `//< intro` markers and the `// vulkan_guide.h : Include file for…` header comments come from vkguide.dev. They don't mean anything to a reader of this repo. Replace them with a one-line comment that says what each file is for.

### C4. Use one asset pipeline

Assimp `Model` and fastgltf `LoadedGLTF` have separate draw paths. The Assimp path is the one with the bugs in section B. Convert Assimp output into `MeshAsset` and `GeoSurface`, using the default material when none is present, so every object goes through `RenderObject`, culling and sorting.

### C5. Write `draw()` as a sequence of named passes

`draw()` is about 180 lines of barriers, descriptor writes and dispatches. Split it into `pass_background`, `pass_geometry`, `pass_tonemap`, `pass_present_blit` and `pass_ui`, and have each pass own its incoming barriers. `draw()` then reads like the diagram in C1, and the change is a natural first step toward [PHASE5_MONOLITH_BREAKUP.md](PHASE5_MONOLITH_BREAKUP.md).

### C6. Merge the duplicated upload helpers

`uploadMesh` and `create_image` each have two overloads that differ only in whether they submit right away. `loadGltf` also writes its own copy of `immediate_submit`. Replace all of these with one helper:

```cpp
UploadBatch batch(engine);              // begins the command buffer
auto mesh  = batch.upload_mesh(indices, vertices);
auto image = batch.upload_image(pixels, extent, format, /*mipmapped*/ true);
batch.submit_and_wait();                // or later: submit_async() → timeline value
```

This also prepares for [background streaming](ADVANCED_PROJECT_IDEAS.md#3-background-asset-streaming).

### C7. Pick one naming convention

The code mixes `uploadMesh`, `handleImGui` and `sendModelDataToGpu` with `draw_geometry`, `init_vulkan` and `resize_swapchain`, and only some members use the `_` prefix. Choose one convention, apply it, and write it down in the README.

### C8. Remove working-directory-relative paths

The shader path `"../spirv/"` (`shared/vk_pipelines.cpp`) and the asset path `"..\\assets\\structure.glb"` (`vk_engine.cpp:1254`) only work when the app is launched from `bin/`. The asset path also only works on Windows. Pass a project root in from CMake with `target_compile_definitions`, or build paths relative to the executable with `SDL_GetBasePath()`.

## D. Advanced GPU techniques

These complement the five projects in [ADVANCED_PROJECT_IDEAS.md](ADVANCED_PROJECT_IDEAS.md), which cover PBR, GPU-driven indirect drawing, streaming, an editor and profiling. Each builds on features the engine already has:

- buffer device addresses (BDA)
- bindless textures
- timeline semaphores
- reversed-Z depth
- dynamic rendering
- `renderScale`

### D1. Meshlets and mesh shaders (`VK_EXT_mesh_shader`)

Split meshes into meshlets with [meshoptimizer](https://github.com/zeux/meshoptimizer) during import. Each meshlet holds up to 64 vertices and 124 triangles and has a bounding sphere and a normal cone. A task shader culls meshlets against the frustum and by normal cone, then the mesh shader emits the survivors. Keep the current vertex-shader path as a fallback when the extension is missing. This is the strongest single showcase item.

### D2. Two-phase occlusion culling with a Hi-Z pyramid

1. Draw the objects that were visible last frame.
2. Build a depth pyramid in a single compute dispatch, in the style of AMD's single-pass downsampler, using subgroup operations.
3. Test the remaining objects against the pyramid and draw the ones that pass.

This builds on the indirect-draw path from [ADVANCED §2](ADVANCED_PROJECT_IDEAS.md#2-gpu-driven-rendering-and-mesh-lod), and with D1 it can cull individual meshlets.

### D3. Visibility-buffer rendering

The geometry pass writes only `(instanceID, triangleID)` to an `R32G32_UINT` target. A compute resolve then fetches vertices through BDA, rebuilds barycentrics and derivatives, and shades each pixel once. Overdraw stops costing shading work, and the existing bindless and BDA design is already most of what this needs.

### D4. Ray-query shadows and ambient occlusion (`VK_KHR_ray_query`)

Build bottom-level acceleration structures (BLASes) straight from the existing vertex and index buffer device addresses, with one top-level acceleration structure (TLAS) per scene. Trace shadow and AO rays inline from the fragment shader or the D3 resolve. You need no separate ray-tracing pipeline or shader binding table.

### D5. Auto-exposure with a compute luminance histogram

Replace the fixed Reinhard curve in `post_process_comp.slang` with these steps:

1. Build a 256-bin log-luminance histogram with shared-memory atomics, reduced with `WaveActiveSum` or subgroup operations.
2. Average the histogram in a single workgroup.
3. Adapt exposure over time and apply an ACES or AgX curve.

The project is small and the result is easy to see, and it shows off subgroup operations.

### D6. Temporal anti-aliasing and upscaling

`renderScale` already exists. Add:

- per-frame sub-pixel jitter on the projection
- a motion-vector target
- history reprojection
- neighborhood color clamping, in YCoCg
- a sharpening pass

Rendering at 0.67× scale should then look close to native resolution.

### D7. Async compute queue

Request a dedicated compute queue family. Run Hi-Z construction (D2), the histogram (D5) and post-processing there, overlapped with the next frame's geometry pass. Synchronize with the existing timeline semaphore and use queue-family ownership transfers. Check the overlap with GPU timestamps.

### D8. Compute-generated image-based lighting

Load an HDR environment map, then generate these resources in compute shaders:

- order-2 spherical harmonics (SH) for diffuse irradiance, replacing the hard-coded "grace" coefficients in `mesh_pbr.slang`
- a GGX-prefiltered specular mip chain
- a split-sum BRDF lookup texture

This pairs with the PBR work in [ADVANCED §1](ADVANCED_PROJECT_IDEAS.md#1-complete-the-gltf-pbr-material-pipeline).

## Suggested roadmap

1. **Correctness and quick performance fixes:** A1, A2 and section B. These take about a day, and the result is correct images and real CPU/GPU overlap.
2. **Clean base for readers:** section C and the README.
3. **Showcase chain:** GPU-driven indirect drawing ([ADVANCED §2](ADVANCED_PROJECT_IDEAS.md#2-gpu-driven-rendering-and-mesh-lod)), then meshlets and mesh shaders (D1), then two-phase Hi-Z culling (D2). Each step can be measured against the previous one.
4. **Visual upgrades:** visibility buffer (D3), ray-query shadows (D4), TAA (D6) and auto-exposure (D5).

## Checking each change

- Build, and keep a clean validation-layer log with sync validation on.
- Compare the Stats panel before and after. Add GPU timestamps first so that improvements to GPU time can be seen.
- Take a RenderDoc capture to confirm pass order, barriers and resource formats. This is especially useful for the sRGB fix and D3.
