# Advanced project ideas for vk-playground

These ideas build on the current renderer and editor. Each one has a concrete result to aim for and links to the relevant Vulkan documentation. Some overlap with the existing [Vulkan modernization plan](VULKAN_MODERNIZATION_PLAN.md); that plan is a roadmap, so check the implementation before treating an item as complete.

## 1. Complete the glTF PBR material pipeline

**Why here:** The glTF loader reads metallic and roughness factors and registers textures in `src/vk_loader.cpp`. The shader in `shaders/mesh_pbr.slang` still computes color from a fixed light direction and base color, without a metallic-roughness BRDF.

**Build:**

1. Implement a metallic-roughness BRDF using the loaded factors and texture channels.
2. Add normal, occlusion, and emissive texture handling, with defined defaults for missing maps.
3. Add environment-based diffuse and specular lighting, then a shadow technique.
4. Create a material comparison scene with metals, dielectrics, and a range of roughness values.

**Done when:** Material changes produce the expected visible differences, and the comparison scene renders without validation errors.

**Vulkan docs:** [PBR for glTF models](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Loading_Models/05_pbr_rendering.html), [PBR lighting implementation](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Lighting_Materials/04_lighting_implementation.html). For an optional ray-query shadow extension, see [ray tracing and acceleration structures](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html).

## 2. GPU-driven rendering and mesh LOD

**Why here:** `src/vk_engine.cpp` currently calls `is_visible()` on the CPU and records individual `vkCmdDrawIndexed` calls. That is a useful baseline for measuring a GPU-driven path.

**Build:**

1. Define compact per-object GPU data: bounds, transform, mesh range, and material ID.
2. Generate several mesh LODs during asset import and choose an LOD by projected size.
3. Add a compute pass that culls objects and writes indexed indirect commands and a draw count.
4. Draw the visible set with `vkCmdDrawIndexedIndirectCount`, with a feature check and the current CPU path as a fallback.
5. Compare both paths on a repeatable scene with thousands of objects.

**Done when:** The same scene renders correctly on both paths, and the GPU-driven path reduces CPU draw-submission time at scale.

**Vulkan docs:** [GPU-driven multi-draw indirect](https://docs.vulkan.org/tutorial/latest/Advanced_Vulkan_Compute/07_GPU_Driven_Pipelines/04_multi_draw_indirect_mdi.html), [`vkCmdDrawIndexedIndirectCount` reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirectCount.html).

## 3. Background asset streaming

**Why here:** Scene loading waits on an upload fence in `src/vk_loader.cpp`; `VulkanEngine::immediate_submit()` also waits for completion. The engine already uses a timeline semaphore for frame synchronization.

**Build:**

1. Move file reads, image decoding, and mesh preparation off the render thread.
2. Batch GPU copies through an upload queue and signal completion with a timeline semaphore.
3. Show placeholder assets until uploads finish; publish new descriptors only when their frame is safe to update.
4. Add an upload budget and loading progress indicator so large scenes remain responsive.

**Done when:** A large scene can load while the camera and UI remain responsive, without sampling unfinished resources or producing validation errors.

**Vulkan docs:** [Synchronization and streaming](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Advanced_Topics/Synchronization_and_Streaming.html), [timeline semaphores](https://docs.vulkan.org/tutorial/latest/Advanced_Vulkan_Compute/08_Asynchronous_Compute/03_timeline_semaphores.html).

## 4. Turn the viewport into a scene editor

**Why here:** `src/app/ui.cpp` has working model loading, while the New and Save menu actions and the Add and View menus are currently empty.

**Build:**

1. Add a scene hierarchy and a properties panel for object transforms and materials.
2. Add viewport object picking, selection highlighting, and transform controls.
3. Serialize scene instances, transforms, camera settings, and asset references; wire up New, Save, and Load.
4. Add undo/redo for common edits.

**Done when:** A user can arrange objects, save a scene, reopen it, and get the same arrangement and material settings.

**Vulkan docs:** [Vulkan tutorial: UI elements and object picking](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/GUI/04_ui_elements.html). Scene serialization and undo/redo are application-level work rather than Vulkan API features.

## 5. GPU profiling and regression scenes

**Why here:** The current ImGui stats panel in `src/vk_engine.cpp` shows frame time and draw counts, but the renderer has no GPU timestamp query instrumentation or named render passes.

**Build:**

1. Add timestamp queries around the background, geometry, post-process, and UI passes.
2. Add debug labels and object names to make frame captures readable.
3. Create fixed camera and scene presets for material, upload, and high draw-count benchmarks.
4. Record CPU time, GPU pass time, draw count, triangle count, and memory use for each preset.

**Done when:** A rendering change can be compared against a stable baseline, and a frame capture identifies each major pass by name.

**Vulkan docs:** [Timestamp query sample](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html), [debug utilities and labels](https://docs.vulkan.org/guide/latest/extensions/VK_EXT_debug_utils.html), [RenderDoc debugging guide](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Tooling/03_debugging_and_renderdoc.html).

## Suggested order

Start with **GPU profiling** if the goal is engine performance work: it gives the GPU-driven and streaming projects a reliable baseline. Start with **PBR** if the goal is a visible rendering upgrade. The scene editor can grow alongside either track. The existing [monolith breakup plan](PHASE5_MONOLITH_BREAKUP.md) would make larger renderer changes easier to isolate, but it is not a prerequisite for a small first milestone.
