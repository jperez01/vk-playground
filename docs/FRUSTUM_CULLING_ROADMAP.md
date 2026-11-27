# Frustum culling: completion roadmap and session handoff

Prepared: 2026-11-23  
Updated: 2026-11-27  
Branch: `feature/aabb-frustum-culling`

This repository document is the canonical roadmap after merge. A matching snapshot is saved in the personal vault's `AI Docs/OMP` folder for convenient reading; update the repository version as implementation progresses.

## 1. What “complete” means

There are two distinct goals:

1. **CPU frustum culling:** conservatively reject draw surfaces outside the camera's viewing volume. The implementation is present; remaining work is closing the correctness and validation checklist below.
2. **GPU-driven frustum culling:** let compute determine visibility and produce commands consumed by drawing. This is a subsequent learning/performance project, not a requirement for calling the CPU implementation complete.

CPU-generated indirect drawing is the bridge between them. Batching and command compaction are later optimizations. Occlusion culling, frame graphs, mesh shaders, and temporal reconstruction are separate projects—not hidden completion requirements.

The correctness invariant throughout:

> Reject only when a surface's conservative bounds prove it is outside. Keeping an invisible surface costs work; rejecting visible geometry is a rendering bug.

Passing the frustum test means **potentially visible**. An object behind a wall can still pass; depth testing and occlusion culling address different questions.

## 2. Current implementation and ownership

### Reusable math

`shared/rendering_math.h` and `shared/rendering_math.cpp`, built by the `rendering_math` CMake library:

- `learning::computeWorldBounds(localCenter, localHalfExtents, worldTransform)` builds a world-axis-aligned enclosure from eight transformed corners. Storage is a fixed array, not a per-object heap allocation.
- `learning::extractFrustumPlanes(viewProjection)` extracts inward-facing, unnormalized world-space planes from `projection * view`.
- `learning::isBoxPotentiallyVisible(center, halfExtents, planes)` rejects a box when any plane excludes its entire extent.

The namespace remains `learning` so existing callers stay consistent. No duplicate implementation is kept in the renderer or exercise runner.

The library exports its include directory, C++20 requirement, GLM dependency, and `GLM_FORCE_DEPTH_ZERO_TO_ONE`. Consumers include `rendering_math.h` and link `rendering_math`; they never include a `.cpp` file or manually duplicate include paths.

### Renderer integration

`VulkanEngine::drawGeometry` in `src/vk_engine.cpp`:

1. Extracts camera planes once when frustum culling is enabled.
2. Converts each candidate surface's local bounds into a world-space box.
3. Filters opaque and transparent candidates using the shared box test.
4. Retains opaque state sorting and transparent back-to-front sorting.
5. Records the existing direct indexed draws.

Loader `Bounds::extents` are already half-extents. `RenderObject::transform` includes the node's world transform. Parent changes therefore affect the bounds computed by this path without adding a separate cache.

The previous engine-local sphere culling and plane extraction helpers have been removed. This is a clean cutover, not a second independent implementation to maintain.

### GPU culling path (`View -> GPU culling`)

`VulkanEngine::useGpuCulling` selects `drawGeometryGpuCulled` instead of `drawGeometry` at one call site in `recordGeometryPass`. The CPU path contains no GPU branches.

1. `prepareGpuCulling` builds unculled, sorted opaque and transparent lists, then walks them in draw order. Slot `s` is the `s`-th surface drawn: all opaque first, then transparent far to near. For each slot it writes world bounds (`computeWorldBounds`) into `cullBoundsBuffer` and a `VkDrawIndexedIndirectCommand` into `indirectBuffer`, both per frame slot, then flushes them.
2. `recordGpuCullDispatch` pushes the six planes, dispatches `shaders/frustum_cull_comp.slang` (one thread per slot, workgroup size 64), then records one barrier from compute shader write to draw-indirect read and one to host read of the counters. It runs before `vkCmdBeginRendering` because a dispatch cannot be recorded inside a rendering instance.
3. The shader writes `instanceCount = 0` or `1` for every slot on every dispatch and atomically counts visible draws and triangles.
4. `drawGeometryGpuCulled` issues one `vkCmdDrawIndexedIndirect` per slot, reading record `s`. It does not re-sort; a mismatch between listed surfaces and slots aborts.
5. `collectGpuCullStats` publishes a frame slot's counters the next time that slot is used, after `beginFrame`'s timeline wait, so the read does not stall. The Stats window therefore shows the shader's visible draw and triangle counts two frames late. The CPU never learns per-surface decisions.

The CPU **Frustum culling** toggle is ignored in this mode. The CPU/GPU layouts (`GPUCullObject`, `GPUCullPushConstants`, `GPUCullStats`) live in `shared/vk_types.h` with size assertions; the Slang structs must change with them. This version issues one call per candidate and is not a draw-call reduction.

### Controls and diagnostics

- **View -> Frustum culling:** default enabled; disabling it bypasses bounds/visibility work and submits all candidate surfaces.
- **Stats -> culled surfaces:** rejected surfaces / candidate surfaces, not scene-node counts.
- Existing draw and triangle counters show the resulting submission workload.
- `EngineStats::candidateSurfaces` and `culledSurfaces` hold the counts; `VulkanEngine::frustumCullingEnabled` controls the path.
- **View -> GPU culling:** default off. With it on, Stats draw calls, triangles, and culled/candidate surfaces come from the shader's counters, two frames late. For about two frames after enabling it, Stats still show earlier CPU values. Draw calls then means visible draws, not API calls.

The renderer still draws across the full window underneath opaque editor panes. Do not assume the central dock opening defines the projection/frustum independently.

### Tests

`exercises/rendering_exercises.cpp` is one combined runner, linked against the same production math library as `demo`. It currently contains **111 checks**:

- 99 box/plane and extraction checks, including canonical Vulkan depth, a clip-space reference comparison, camera translation/rotation, finite reversed-Z, and the Vulkan Y flip.
- 12 world-bounds checks covering translations, nonuniform/negative scaling, rotations, parent-child transforms, shear, collapsed axes, and zero-size boxes.

Run from an MSVC developer environment:

```powershell
cmake --build cmake-build-debug-visual-studio --target rendering_exercises
ctest --test-dir cmake-build-debug-visual-studio -R "^rendering_exercises$" --output-on-failure
```

The runner is excluded from normal builds and its executable stays inside the selected build tree. There is no test `main()` or test-build macro in the reusable math library.

### Window lifecycle fix included in this branch

A Debug lifecycle smoke run reported `VUID-VkSwapchainCreateInfoKHR-imageExtent-01689`: swapchain creation received a zero extent during window minimization/resizing.

The fix:

- Checks minimized state and drawable size.
- Checks Vulkan surface capabilities before retiring existing swapchain resources.
- Makes recreation report success/deferral; `run()` skips the frame while recreation remains pending.
- Adds a small local vk-bootstrap guard at its final extent selection, returning out-of-date before issuing an invalid zero-extent creation.
- Creates render targets using the actual swapchain extent.

A late surface change can still cause creation to defer after old resources were retired. That state deliberately keeps a null swapchain and a pending resize; no acquisition/rendering may occur until recreation succeeds. Existing graphics-completion and presentation-fence retirement remain in place.

## 3. Verification already obtained—and its limits

- Debug builds passed for the culling integration and subsequent window fix.
- The shared-math exercise runner passed all 111 checks.
- Live culling controls and counters were exercised with the real local `structure_mat.glb` asset. Observed counts included 48 submitted draws from 457 candidates with culling enabled and 457 with it disabled.
- The camera changed between those captures. **Do not claim a same-pose pixel-equivalence test or a speedup from those numbers.**
- The user reported that culling worked and later reported that the minimize/restore fix seemed to work.
- A targeted renderer session closed normally with no captured validation error lines. The earlier lifecycle smoke still recorded the zero-extent error before the window fix.
- **Post-window-fix validation output has not been independently reviewed.** User-reported recovery is runtime feedback, not proof that the validation log is clean.
- No optimized performance benchmark has been recorded.
- GPU culling: Debug build passed. The user reported the GPU path working in the app after the stats readback was added. No same-pose CPU/GPU count comparison is recorded here, Debug validation output for this path has not been reviewed, and no timings exist.

The old lifecycle error is not an allowed baseline. If it reappears, investigate it rather than suppressing it or describing that run as validation-clean.

## 4. Milestone A: close the CPU correctness baseline

Do this before adding GPU-generated work.

### Work

- [ ] Review post-fix Debug stdout/stderr through normal shutdown.
- [ ] With a stationary camera, compare culling enabled and disabled on a real scene: visible geometry should not disappear.
- [ ] Move objects/camera across side and depth boundaries; keep intersecting and touching bounds conservatively.
- [ ] Check objects wholly behind the camera and beyond the finite depth range.
- [ ] Exercise both opaque and transparent content. Preserve transparent ordering.
- [ ] Exercise translation, rotation, nonuniform/negative scale, parent transforms, and model replacement in the integrated renderer.
- [ ] Minimize, restore, resize, and close normally; verify rendering resumes and no validation errors are reported.
- [ ] Record the result and any untested scenarios here.

### Acceptance

The user confirms normal rendering/recovery, Debug validation output is checked, CPU tests pass, and no known visible-object rejection remains. Candidate/draw counts behave sensibly when culling is toggled. It is acceptable for conservative bounds to keep extra objects.

Finite nondegenerate camera projections are the current math contract. Infinite-far projections and vertex-deformed/skinned meshes require separate handling; do not claim those are supported by rigid-mesh bounds tests.

Floating-point boundary behavior must be considered explicitly. If a tolerance becomes necessary, derive its scaling for unnormalized planes; do not add an arbitrary epsilon to hide an incorrect matrix or coordinate space.

**At this milestone the CPU frustum-culling feature can be called complete.**

## 5. Milestone B: CPU-generated indirect draws

**Status: implemented and user-tested as a stepping stone, then removed as a separate mode.** Milestone C superseded it: the CPU-written-record path and its `useIndirectDraws` toggle were deleted, and the per-slot indirect buffer and flush/lifetime pattern now serve GPU culling. The direct CPU path with CPU frustum culling remains the comparison reference.

### Work

- [x] Trace the direct `vkCmdDrawIndexed` arguments into a `VkDrawIndexedIndirectCommand` record (`indexCount`, `instanceCount = 1`, `firstIndex`, `vertexOffset = 0`, `firstInstance = 0`).
- [x] Allocate writable indirect-command storage per frame slot with `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` and a doubling growth policy (floor 256 records).
- [x] Wait for the slot's previous GPU use before rewriting its records, using the existing timeline wait in `beginFrame`.
- [x] Write records in draw order, not in visibility-loop order.
- [x] Flush host-written ranges.
- [x] Replace each direct call with a one-record `vkCmdDrawIndexedIndirect` call.
- [x] Keep a direct/indirect comparison mode during development (later removed, see status).
- [ ] Verification recorded: the user tested the zero-visible case and direct/indirect comparison. Growth beyond 256 records, model replacement, and resize were not separately recorded, and the Debug validation log was not reviewed.

### Constraints that still apply

An indirect record does not bind a pipeline, index buffer, descriptor set, or per-object push constants. One call per surface is the smallest correctness step; it is **not** a submission optimization claim.

## 6. Milestone C: GPU frustum testing with fixed command slots

Move only visibility decisions onto the GPU. Avoid compaction and GPU-generated draw counts initially.

### Work

- [x] Give each candidate draw surface a stable indirect-command slot (slot = position in final draw order).
- [x] Keep static geometry fields correct and have compute write `instanceCount = 0` or `1` for every candidate each dispatch.
- [x] Upload world-space bounds (storage buffer) and camera planes (push constants) for the frame.
- [x] Use an explicit GPU data layout with matching padded fields and `static_assert`s on size and offsets.
- [x] Include `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` on buffers written or read as storage.
- [x] Start on the existing graphics/compute-capable queue, not async compute.
- [x] Add the same-queue dependency from compute shader write to draw-indirect command read.
- [x] Vertex-shader storage-read dependency: not applicable, because compute writes only `instanceCount`, which vertex shaders do not read.
- [x] Retain per-slot lifetime protection (per-slot buffers, `beginFrame` timeline wait).
- [ ] Verify dispatch sizing and shader bounds checks for counts not divisible by the workgroup size: implemented (`ceil(n / 64)` groups, `slot >= objectCount` guard) but not specifically exercised with a recorded non-multiple count.
- [ ] Compare GPU visibility decisions with the CPU reference: partial. Shader atomic counters are read back after the slot's existing timeline wait, with no extra stall, giving aggregate visible draws and triangles. No per-surface comparison and no recorded same-pose CPU/GPU count comparison exist.

### Critical edge cases

Zero/all-visible/all-hidden candidates; objects entering and leaving the frustum; moving parents; rotations/nonuniform scale; camera cuts; multiple surfaces per mesh; reload/replacement; count/capacity changes; frame-slot reuse; and normal cleanup.

A visible-only writer leaves stale commands for objects that later become hidden. Every fixed slot needs a current-frame decision.

Keep transforms used for rendering and bounds used for compute consistent. A CPU scene object updated this frame can leave another frame slot stale: track updates per slot or use versions. The latest value is sufficient for ordinary rendering; temporal effects may need additional history.

The original per-draw push constants and bindings can remain while the CPU still issues one call for each candidate. A compact visible-ID list is **not required** for this first GPU-culling version.

### Acceptance

The GPU decisions agree with the CPU reference under the documented numerical/conservative policy. No visible geometry disappears, no stale slot remains visible, lifetime/barrier checks pass, and Debug validation is clean for exercised scenarios.

**At this milestone basic GPU frustum culling is functionally complete.** It need not yet reduce CPU draw-call overhead.

## 7. Milestone D: batch and compact only when measurements justify it

This stage turns a working GPU-culling path into a potential submission optimization.

### Work

- [ ] Group draws only when pipeline, descriptor/material access strategy, and bound index-buffer state are compatible.
- [ ] Replace per-draw object push constants with an appropriate GPU-readable object-data lookup before expecting many objects to share one draw call.
- [ ] Choose an object-ID strategy deliberately: draw ID, instance index/first instance, or a visible-ID indirection. Define which index addresses which array.
- [ ] Check and enable relevant device features/limits. Examples: `multiDrawIndirect` for multiple commands per call, `drawIndirectFirstInstance` for nonzero indirect first-instance values, and `drawIndirectCount` if adopting GPU-generated counts.
- [ ] Measure fixed-slot multi-draw before adding compaction; zero-instance commands are a valid baseline.
- [ ] If compaction is warranted, reset counters each frame, define capacity, and handle overflow without silently dropping visible work.
- [ ] Add compute dependencies for command buffers, count buffers, and any visible-ID/object buffers according to their actual consumers.
- [ ] Preserve transparent ordering. Unordered atomic compaction is not a substitute for back-to-front sorting; keeping transparency on the existing path is acceptable if explicitly documented.

### Acceptance

Batching/compaction preserves the accepted rendering behavior and is compared with the direct and fixed-slot references. Unsupported features are handled explicitly; do not assume all devices support the preferred path.

This stage is optional if profiling shows no useful gain. A more complex implementation that does not help is not a better completion state.

## 8. Milestone E: evaluate cost and simplify

Use an explicitly optimized profile for performance, not Debug validation timings. The configured profiles share `bin/`: never build them concurrently or overwrite a running renderer, and follow the project profile/relink procedure.

Compare at least a small scene, many candidates with high visibility, and many candidates with low visibility. Keep scene, camera path, resolution, settings, build flags, and presentation pacing controlled. Warm up and report repeated measurements/variability.

Record CPU culling/recording time, GPU culling cost, total GPU/frame time, submission counts, memory, and relevant visual correctness. Do not infer total frame improvement from fewer draw calls or a faster isolated pass. Capture/readback instrumentation can change timing.

Possible targeted improvements after evidence:

- Cache world bounds when world transforms or geometry change; parent updates must invalidate descendants.
- Track dirty object uploads per frame slot so all copies eventually become current.
- Compare the eight-corner reference with the affine absolute-matrix bounds formula.
- Tune compute workgroup/data layout only after identifying a bottleneck.

Keep useful reference/debug modes intentionally; remove abandoned paths and temporary instrumentation. Do not grow this into a general renderer rewrite.

## 9. Verification workflow for future sessions

The user prefers short, evidence-first iterations:

1. Explain the scoped change and identify its key invariant.
2. Implement scaffolding/CMake/test setup automatically for function exercises; leave internals to the user unless implementation is explicitly requested.
3. Keep related reusable functions in the shared header/source pair and all exercise checks in the existing runner. Do not create files or targets per tiny function.
4. Run focused builds/automated checks.
5. Explain what changed and give short manual test steps. Let the user run the app and report results.
6. Diagnose with validation output, counters, targeted terminal logs, or a debugger. Add logging only for a specific question.
7. Avoid lengthy automated screenshot/window/mouse sessions unless requested or agreed necessary. If a spatial illustration helps, use a geometric PNG; SVG blocks did not display reliably in this client.
8. Report build, runtime/visual, validation, and performance separately. Mark unobserved checks as pending, not passed. Do not rerun a user-reported failure merely to confirm it.

Use `renderer-check`, `vulkan-review`, and `windows-native-check` as applicable. Native LSP has returned stale references in this environment; verify locations and use source inspection when the index is unreliable, without claiming complete semantic coverage.

The assistant may delegate sufficiently scoped routine work to the configured GPT Luna `routine` worker, but retains architecture, correctness-sensitive decisions, and integration. Direct chat replies use the selected chat model; do not promise an automatic per-message model switch.

## 10. Future-session starter prompt

> Continue `docs/FRUSTUM_CULLING_ROADMAP.md`. First inspect current code and identify the next unfinished milestone; do not assume this snapshot is still current. Keep the scope to one milestone. I am learning rendering/engine engineering: provide scaffolding and tests, let me implement the central learning slice, and review my reasoning. Reuse `shared/rendering_math.*` and the combined exercise runner. Run focused automated checks, then let me test the app; no lengthy screenshot automation. Preserve validation diagnostics and report unverified scenarios. Milestone A (CPU baseline) is still open, and GPU fixed-slot culling (Milestone C) is implemented but unvalidated: review its Debug validation log and compare CPU/GPU counts at a fixed pose before starting Milestone D batching, which is optional and measurement-driven.

## 11. Completion record

Update this section as evidence is obtained:

- CPU implementation and shared tests: **implemented; 111 checks passed**.
- Integrated culling runtime: **user reported working; counters exercised; fixed-pose equivalence not established in the interrupted automated comparison**.
- Zero-extent window fix: **Debug build passed; user reported recovery; post-fix validation-log review pending**.
- CPU-generated indirect drawing: **implemented and user-tested; the separate mode was later removed in favor of GPU culling**.
- GPU fixed-slot visibility: **implemented; Debug build passed; user reported it working in the app; validation-log review, fixed-pose CPU/GPU count comparison, and per-surface comparison pending**.
- Batching/compaction: **not started; optional and measurement-driven**.
- Optimized performance comparison: **not recorded**.
