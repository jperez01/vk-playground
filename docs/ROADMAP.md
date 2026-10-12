# Roadmap

This file tracks planned and completed work on vk-playground. Each item says what changes, why, and where. Update the status when a PR lands.

Status key: ✅ merged · 🚧 in progress · ⬜ planned

## Completed

| PR | Summary |
|---|---|
| #8 | The CPU and GPU now overlap frames: each frame waits on its own frame slot's timeline value. Validation layers run only in debug builds. Fixes: the post-process shader path, sRGB base-color textures, swapped texture extents, the Assimp draw path, and an ImGui format string. |
| #10 | Uploads for imported models are batched. Changes: glTF textures get mipmaps, mipmap generation works for non-square textures, bindless texture slots are deduplicated and bounds-checked, each glTF file uses one vertex and one index buffer, the background pass dispatches at `drawExtent`, VMA uses `AUTO` usage, the present mode can be selected, and the CPU timing label is accurate. |
| #11 | `GpuProfiler`: per-pass GPU timestamps and debug labels. Member variables no longer use a leading underscore. |
| #12 | Adds the README. Removes dead shaders, dead code and tutorial markers. Shader and asset paths are defined in CMake. Functions and members use camelCase. |
| #13 | `UploadBatch` and `UploadContext` replace the duplicated upload helpers. Imported models load as a regular `LoadedScene`. `draw()` is split into named passes. The bindless binding uses `UPDATE_AFTER_BIND`. `loadModel` lives in `vk_loader`. |
| #14 | Fixes in the glTF loader: name collisions, textures without samplers, and primitives without materials. Render targets are recreated on resize. Embedded Assimp textures load. |

## PR B: simpler code and dependencies 🚧

Branch `simplify-structure`. This PR should not change behavior, except for the two bug fixes marked **(bug)**.

| # | Change | Why |
|---|---|---|
| B1 | Delete dead code: `shared/meshes.*`, `VulkanEngine::mainCamera`, the `renderNodes()` declaration, `vkutil::destroyImage/destroyBuffer`, unused `vkinit` helpers, and unused includes. | Less code to read. Nothing uses any of it. |
| B2 | Add `vkutil::createComputePipeline(device, shader, layout)` and use it for the gradient, sky and post-process pipelines. | Removes three copies of the same 15 lines of boilerplate. |
| B3 | Move `createBuffer`, `createImage` and `HOST_WRITE_MAPPED` from `src/utils/util.*` to `shared/vk_resources.*`, and delete `src/utils/`. | All of `vkutil` then lives in one place. |
| B4 | Rename `GLTFMetallic_Roughness` to `MeshPipelines`, and `metalRoughMaterial` to `meshPipelines`. | The struct holds the opaque and transparent mesh pipelines for every scene type, not only glTF. |
| B5 | Replace per-frame `FrameGarbage` with a `DeletionQueue` in `shared/`. Each entry is tagged with the timeline value of the next submit and is destroyed once the GPU passes that value. This removes `VulkanEngine::Get()` and the `vkutil::deferDestroy*` functions that `src/vk_engine.cpp` defined for `shared/`. **(bug)** | Today `shared/` calls back into the executable through a global singleton. Also, resources freed between frames, such as a scene replaced from the UI, go into the current slot's garbage list, and that list is flushed right after waiting on a submit from *two* frames earlier, so the previous frame may still be using them. |
| B6 | Gather the scene types (`Bounds`, `GeoSurface`, `MeshAsset`, `GLTFMaterial`, `Node`, `MeshNode`, `RenderObject`, `DrawContext`, `LoadedScene`) into `src/scene.h/.cpp` and drop `IRenderable`. `vk_types.h` keeps only GPU resource and layout types, and `vk_loader.h` only declares `loadGltf` and `loadModel`. | Today these types are spread across `vk_types.h`, `vk_engine.h` and `vk_loader.h`. |
| B7 | Build the draw list (`updateScene`) after the UI has run each frame, not before it. **(bug)** | Today, loading a model whose path is already loaded frees the old scene after `updateScene` has stored raw pointers to its materials, and `drawGeometry` then dereferences them. |

## PR A: frame optimizations ⬜

Measure each change with the GPU timings in the **Stats** window, comparing before and after.

| # | Change | Expected effect |
|---|---|---|
| A1 | The geometry pass clears the color target (`attachmentInfo(..., &clearValue, ...)` gives `loadOp = CLEAR`), which erases the background compute output. Use `LOAD`. **(bug)** | The gradient and sky effects become visible, and the Background pass stops being wasted work. |
| A2 | Replace the compute tonemap, the `postProcessingImage` and the blit with one full-screen fragment pass that samples `drawImage` and writes the swapchain. Draw ImGui in the same render pass. | Removes one full-screen RGBA16F image write and read, one blit, and three barriers. The sampler handles `renderScale`. |
| A3 | Change `drawImage` from `R16G16B16A16_SFLOAT` to `B10G11R11_UFLOAT_PACK32`. | Half the bandwidth in the background, geometry and tonemap passes, since alpha is unused. |
| A4 | Cull with the bounding sphere (`bounds.sphereRadius`) against six frustum planes extracted once per frame, instead of projecting 8 corners per object. Reuse the draw-list vectors instead of allocating them every frame. | Lower CPU record time. |
| A5 | Sort transparent surfaces back to front. **(bug)** | Correct blending. |
| A6 | Use `UINT64_MAX` as the frame-wait timeout. Today a 1-second `VK_CHECK` timeout aborts on a long GPU hitch. **(bug)** | No abort when the GPU stalls. |
| A7 | Enable anisotropic filtering on texture samplers. | Sharper textures at grazing angles, at almost no cost. |

## PR C: split the engine ⬜

Split `src/vk_engine.cpp` (about 1,450 lines) into a `VulkanContext` (instance, device, queues, swapchain) and a `Renderer` (passes, pipelines, per-frame data), following [PHASE5_MONOLITH_BREAKUP.md](PHASE5_MONOLITH_BREAKUP.md). This is easier after PR B.

## Later: GPU-driven rendering and advanced techniques ⬜

Each item depends on the one before it:

1. **GPU-driven indirect drawing:** per-object data in a storage buffer, compute frustum culling, and `vkCmdDrawIndexedIndirectCount`, with the CPU path kept as a toggle.
2. **Meshlets and mesh shaders** (`VK_EXT_mesh_shader`), with per-meshlet frustum and normal-cone culling in a task shader.
3. **Two-phase occlusion culling** against a Hi-Z depth pyramid.

Other candidates: a visibility buffer, ray-query shadows and AO, TAA with upscaling through `renderScale`, auto-exposure from a luminance histogram, an async compute queue, and compute-generated IBL.

### Known issues not yet scheduled

- `Model` finds a model's directory with `find_last_of('\\')`, which only works with Windows paths. It should use `std::filesystem::path::parent_path()`.
- Assimp node transforms are ignored: every imported mesh is placed at the origin.
- Resources are only freed when a scene is replaced. There is no way to unload a scene from the UI.
