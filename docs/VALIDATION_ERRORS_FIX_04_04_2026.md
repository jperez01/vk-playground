# Branch Changes Summary (vs `master`)

> Note: I could not run `git diff master...HEAD` in this environment (terminal unavailable), so this summary is based on the current branch state and the updated source/docs in the workspace.

## 1) Rendering + Synchronization fixes

**Files:** `src/vk_engine.cpp`, `src/vk_engine.h`, `shared/vk_images.cpp`

- Added per-swapchain-image render-finished semaphores (`swapchainRenderSemaphores`) and wired present waits to those semaphores.
  - **Why:** fix binary semaphore reuse hazards during acquire/present cycles.
- Added swapchain image layout tracking (`swapchainImageInitialized`) and used `UNDEFINED` on first-use, `PRESENT_SRC_KHR` after first present.
  - **Why:** fix first-frame layout mismatch when transitioning swapchain images.
- Updated submit wait/signal stage masks in `draw()` (broadened waits/signals where needed).
  - **Why:** remove acquire/transition synchronization warnings.
- Updated render extents to use actual attachment-compatible extents (`drawExtent` / `swapchainExtent`) in rendering paths.
  - **Why:** avoid dynamic rendering renderArea/imageView size mismatches.
- Tightened transition helper behavior for transfer-src in `shared/vk_images.cpp`.
  - **Why:** reduce overbroad barrier/access usage warnings.

## 2) Descriptor / Pipeline correctness adjustments

**Files:** `src/vk_engine.cpp`, `src/vk_engine.h`

- Post-process pipeline initialization path was effectively disabled from main init flow (`init_post_process_pipeline()` no longer called from `init_pipelines()`).
  - **Why:** post-process descriptors/pipeline were not used in the active render path and were causing descriptor-format/sampler noise.
- Reordered init to ensure default samplers are created before pipeline usage (`init_default_data()` before `init_pipelines()`).
  - **Why:** avoid invalid sampler descriptor update issues.
- `drawImage` usage flags include sampled capability where relevant transitions/descriptors expect it.
  - **Why:** keep image usage/layout paths valid when sampling descriptors are involved.

## 3) Compute/image format alignment

**Files:** `src/vk_engine.cpp`

- `drawImage.imageFormat` is set to `VK_FORMAT_R16G16B16A16_SFLOAT`.
  - **Why:** align with compute shader storage image format expectation (`Rgba16f`) to avoid undefined storage-image dispatch behavior.

## 4) Resource lifetime + shutdown cleanup

**Files:** `src/vk_engine.cpp`

- Added explicit cleanup of imported model GPU resources in `VulkanEngine::cleanup()`:
  - destroys `gpuMeshBuffers` buffers
  - destroys `gpuTextures` images/views/allocations
- Implemented `GLTFMetallic_Roughness::clear_resources()` pipeline/layout destruction with `VK_NULL_HANDLE` guards.
- Hardened compute pipeline destruction lambda (captured concrete handles, null checks).
- Added cleanup for default engine resources created in `init_default_data()`:
  - rectangle buffers
  - default images (`white/grey/black/checkerboard`)
  - default samplers

**Why:** remove invalid pipeline destroy calls and device-shutdown object leaks.

## 5) Documentation changes on this branch

**Files under `docs/` currently present in branch:**
- `INDEX.md`
- `INFORMATION_SOURCES.md`
- `QUICK_REFERENCE.md`
- `README_VALIDATION_FIXES.md`
- `VALIDATION_FIXES_APPLIED.md`
- `VALIDATION_FIXES_SOURCES.md`
- `VERIFICATION_CHECKLIST.md`

**Why:** these were created to track validation triage, applied fixes, and verification guidance during iterative debugging.

---

## Current status from latest shared logs

- Remaining items appear to be mostly **non-fatal warnings** (debug extension, SPIR-V deprecation, VMA performance allocation warnings).
- The prior critical blockers (descriptor mismatch, swapchain layout/sync hazards, many shutdown invalid-handle/leak issues) were addressed by the code-path changes listed above.

If you want, the next focused pass can be only on reducing the remaining non-fatal warning noise (shader recompilation target + allocator pooling strategy), without changing render behavior.

