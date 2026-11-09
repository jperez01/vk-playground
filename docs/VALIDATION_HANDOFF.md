# Vulkan validation work — session handoff

## Completed continuation — 2026-10-31

Implemented on `fix/vulkan-validation-errors`. The original investigation below is historical failing-before context, not the current state. The user subsequently requested publication of these changes as a pull request.

### Corrections

- Acquire waits cover the first transfer use; render-complete signals cover all commands including the final presentation transition.
- `VK_EXT_swapchain_maintenance1` and `swapchainMaintenance1` are required, with `VK_EXT_surface_maintenance1` and `VK_KHR_get_surface_capabilities2` instance dependencies. Each image has a presentation fence; reuse waits/resets it, and destruction waits every pending present before destroying its semaphore/images/swapchain. Graphics timeline waits remain in recreation; device idle remains in cleanup. There is no idle-only compatibility fallback.
- Swapchain creation declares its selected supported present mode; unsupported requests fall back to FIFO. Suboptimal acquire still consumes/presents the acquired image, then recreates. Every submitted frame advances its frame slot even after an out-of-date present.
- Shared HDR, depth and postprocess images order previous-frame uses before discard/reuse.
- Mipmap final transitions use transfer-read access from `TRANSFER_SRC_OPTIMAL`; direct uploads retain transfer-write access. Mesh copies explicitly synchronize to index input and vertex pulling.
- Mapped staging, material and scene-uniform writes flush through VMA for non-coherent memory.
- Scene release removes image/sampler raw-handle lookup keys, avoiding stale bindless cache hits when handles are recycled. Old descriptor elements remain untouched for pending draws. Slots are still append-only; repeated imports can exhaust the existing 1000-slot limit.

### Observed verification

Debug was built with synchronization and best-practices validation unchanged. Device query identified NVIDIA GeForce RTX 5070 Ti and confirmed swapchain maintenance support; the full `vulkaninfoSDK` dump timed out after recording this evidence, so no complete capability inventory is claimed.

| Check | Result |
|---|---|
| Final Debug build | Passed |
| Automated resize, shrink/expand, minimize/restore | Scene and UI screenshots inspected; rendering resumed |
| Automated normal shutdown | Exit 0, no forced termination |
| Automated validation | 0 errors, 14 warnings |
| Targeted interaction run | FIFO → MAILBOX → IMMEDIATE → FIFO; gradient → sky |
| Live imports | `structure_mat.glb` loaded twice through File → Load (replacement), `monkey_flat.obj`, prepared licensed `demo-assets/structure.glb` |
| Targeted rendering / cleanup | Screenshots inspected; rendering continued, normal exit 0 |
| Targeted validation | 0 errors, 14 warnings; stderr empty |

Final automated evidence: `%TEMP%/vk-playground-smoke-ff9bd0c5107c49f8a51dec7be09e942b`.
Targeted evidence: `%TEMP%/vk-validation-targeted-wi_6sexy`.
Both contain stdout/stderr, screenshots and `report.json`. The earlier corrected run `%TEMP%/vk-playground-smoke-cfac4e4bc0a14d77acde9cc3b28daadd` had zero errors but also a missing-present-mode-declaration warning; that warning was corrected before the final runs.

Remaining warnings: three external Galaxy overlay naming messages, plus small dedicated buffer/image allocation performance messages. The buffer-allocation warning reaches the layer's duplicate-message limit; the reported warning count is not an exhaustive allocation count. No validation errors, mipmap layout/access warnings, or missing-present-mode-declaration warnings were observed in the final runs. No diagnostics or external layers were disabled.

Coverage limits: only the selected RTX/driver was executed; the AMD adapter and unsupported-extension startup were not exercised. Imports add to the running reactor scene, not isolated default-scene replacement. Render scale has no current UI control and was not varied. Non-coherent hardware memory was not forced; VMA flush paths were compiled and exercised on this device. FBX, device/surface loss, slot exhaustion, and release-candidate packaging were not tested. Saved ImGui settings and local assets were preserved; no owned renderer remains running.

Presentation-retirement contract: [Khronos presentation fences](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html); [extension requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_swapchain_maintenance1.html).

## Original investigation snapshot

The user requested: **find and fix all Vulkan validation errors**. They then requested this handoff to continue in a fresh session.

- Repository: `C:\Users\jpabl\dev\projects\vk-playground`.
- Working branch created for this work: **`fix/vulkan-validation-errors`**.
- Branch base: `134ba134c55380bcba12d07e5c69a561ef01d4f9` (`Merge pull request #19 from jperez01/ci/windows-demo-builds`).
- **No renderer fixes, feature changes, builds, or renderer runs have been performed during this investigation.** This document is the only intentional repository edit from this validation task so far.
- Nothing from this validation task has been committed, pushed, or opened as a PR.
- Preserve unrelated user-owned untracked files: `docs/ADVANCED_PROJECT_IDEAS.md` and `docs/IMPROVEMENT_IDEAS.md`.
- User preference: make changes on a descriptively named separate branch; continue on the existing validation branch rather than master.

The release/distribution work preceding this task was merged. This task is renderer correctness, not additional release automation.

## Required context and verification rules

Start with the auto-loaded project instructions and `README.md`. Read these skills in the new session:

- `skill://vulkan-review`
- `skill://renderer-check`
- `skill://windows-native-check`

Use native clangd/LSP for definitions and references (`xd://lsp`, read its schema before use). Source line numbers below describe the current investigation snapshot and may move after edits.

Do not disable validation, suppress messages, or count known errors as a passing result. Debug enables synchronization and best-practices validation; RelWithDebInfo does not provide validation proof. Report build, actual rendering, validation, and coverage limits separately.

Both local build profiles write into shared `bin/` and `spirv/` outputs. Do not build profiles concurrently or overwrite a running renderer. Only control renderer processes started by the assistant. Preserve user ImGui settings and local assets.

## Ground-truth baseline evidence

An existing actual Debug smoke run provides failing-before evidence; do not rerun merely to reconfirm these already observed failures:

```text
C:\Users\jpabl\AppData\Local\Temp\vk-playground-smoke-5ebde8a4e2b44098b8901944781fa1b2
```

Read its `report.json`, `renderer.stdout.log`, `renderer.stderr.log`, and screenshots as needed. The current investigation read the stdout diagnostics. Earlier distribution work observed the report and rendered screenshots:

- Debug build passed.
- Renderer exited normally with exit 0; resize/minimize/restore and cleanup exercised.
- Validation failed: **18 errors, 23 warnings**; duplicate-message limit reached.
- Error identifiers: `PRESENT_AFTER_WRITE`, `VUID-vkDestroySemaphore-semaphore-05149`, `VUID-vkDestroySwapchainKHR-swapchain-01282`.
- This run predates the validation branch. Its rendering/synchronization source remains unchanged; release automation subsequently merged.

An older run also recorded the same identifiers:

```text
C:\Users\jpabl\AppData\Local\Temp\vk-playground-smoke-d738d5b9cafa41d581cbc43d24f5f148
```

Do not interpret a duplicate-limited log as an exhaustive inventory of errors.

### Observed errors

1. **`PRESENT_AFTER_WRITE`**: presentation races a layout-transition write to the swapchain image. Repeated for images 0, 1, and 2.
2. **Semaphore destruction while in use**: `vkDestroySemaphore` reports a semaphore still referenced by a queue (`05149`).
3. **Swapchain destruction while in use**: `vkDestroySwapchainKHR` reports outstanding use of its presentable images (`01282`).

Destruction errors repeat for multiple recreated swapchains, strongly connecting them to the recreation path. Their exact solution still requires implementation and runtime verification.

### Observed warnings — separate from errors

- Nine image-barrier warnings: `VK_ACCESS_2_TRANSFER_WRITE_BIT` is inconsistent with an old `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` layout; expected transfer-read access. Source-level cause identified below.
- Small dedicated buffer/image allocations trigger best-practices performance warnings. Ownership has **not** been investigated yet; do not assume these originate in project code rather than ImGui/backend allocation.
- External `GalaxyOverlayVkLayer`, `_VERBOSE`, and `_DEBUG` naming warnings (`Policy #LLP_LAYER_3`). Do not silently disable external layers to manufacture a clean report; distinguish environment warnings from renderer errors.

## Source-level findings

### 1. Acquire wait and presentation signal stages need correction

`src/vk_engine.cpp`:

- `beginFrame`, lines 245–278: waits for the reused frame slot's timeline value, collects deletion queue entries, then calls `vkAcquireNextImageKHR` using the frame's acquire semaphore. Only `VK_ERROR_OUT_OF_DATE_KHR` is explicitly handled.
- `recordBlitPass`, lines 397–424: first swapchain-image use is a transition to `TRANSFER_DST_OPTIMAL`, then a blit. Its initial barrier uses `ALL_TRANSFER` → `TRANSFER`, destination transfer-write access.
- `recordUiPass`, lines 427–452: transfer-write → color-attachment read/write barrier, then ImGui rendering.
- `submitAndPresent`, lines 455–510:
  - Final transition is color attachment → `PRESENT_SRC_KHR`, source color-output/write, destination `NONE`/no access.
  - Acquire semaphore wait currently uses **`COLOR_ATTACHMENT_OUTPUT`**, despite the earlier transfer use.
  - Per-image render-complete semaphore signal currently uses **`ALL_GRAPHICS`**, rather than covering all commands and the final presentation transition.
  - Separate frame timeline signal already uses `ALL_COMMANDS`.
  - `vkQueuePresentKHR` waits on the acquired image's render-complete semaphore.
  - Only `VK_ERROR_OUT_OF_DATE_KHR` is explicitly handled; frame number increments otherwise.

**Likely correction:** acquire wait must cover the first transfer use; render-complete signal must cover the final transition (typically `ALL_COMMANDS`). These are source-backed findings, not a verified fix. Audit success/suboptimal/out-of-date handling without silently adding unrelated scope.

### 2. Recreation waits for graphics completion, not presentation retirement

- `createSwapchain`, lines 872–900: builds via vk-bootstrap; creates one render-complete binary semaphore per swapchain image. **Per-image semaphore indexing is already correct**; do not replace it with per-frame indexing.
- `destroySwapchain`, lines 901–917: destroys render semaphores, clears initialization flags, destroys swapchain, then destroys its image views.
- `resizeSwapchain`, lines 919–948: waits only for the graphics timeline, checks nonzero SDL window dimensions, destroys/recreates the swapchain and render targets, rewrites draw-image descriptor.
- `cleanup`, lines 129–199: calls `vkDeviceWaitIdle` first, eventually invokes `destroySwapchain` at line 187.
- `initVulkan`, lines 733–827: requests Vulkan 1.3, synchronization2/dynamic rendering, descriptor indexing, timeline semaphore, push descriptors. There is currently no explicit swapchain-maintenance/present-fence setup in this code.

**Unresolved design decision:** use a specification-backed presentation-retirement mechanism, including recreation and normal shutdown. Do not assume a graphics timeline wait proves presentation completion. Investigate extension support and vk-bootstrap behavior before changing hardware requirements or selecting a fallback.

Authoritative sources already read:

- [Khronos Guide: Swapchain Semaphore Reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
  - Reacquiring an image and waiting for its acquire signal proves the previous presentation's semaphore wait is complete.
  - Graphics-submit fences/timeline completion do not establish presentation completion.
  - Unextended `vkDeviceWaitIdle`/`vkQueueWaitIdle` do not provide a complete specification guarantee for retiring presentation resources; historically applications rely on them in practice.
  - `VK_EXT_swapchain_maintenance1` provides presentation fences for safe retirement. When enabled, validation expects presentation fences rather than relying only on idle waits.
- [VkSwapchainPresentFenceInfoKHR reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)
  - EXT equivalent is `VkSwapchainPresentFenceInfoEXT`.
  - A signaled presentation fence permits destruction of associated wait semaphores; all past presentation requests must be retired before destroying the swapchain.
  - Fences must be unsignaled and not associated with another incomplete queue operation when attached.

Do not present an idle-only change as a full specification-backed retirement solution without addressing the above distinction.

### 3. Mipmap final barrier has an access/layout mismatch

`shared/vk_images.cpp`:

- `transitionImageForShaderSampling`, lines 97–108: always uses source transfer-write access, destination fragment-shader read.
- `generateMipmaps`, lines 142–211: each level transitions transfer-destination → transfer-source with transfer-write → transfer-read dependency, then blits to the next level.
- At line 210 it calls the above helper with **old layout `TRANSFER_SRC_OPTIMAL`**, but the helper still supplies transfer-write source access. This directly matches the observed warnings.

LSP confirmed `generateMipmaps` is called by `UploadBatch::uploadImage` (`src/upload_batch.cpp:97`). The non-mipmapped branch at lines 99–100 uses the same shader-sampling helper with `TRANSFER_DST_OPTIMAL`; retain the correct write-to-read dependency for that branch when fixing the mipmapped path. Do not indiscriminately change both callers to transfer-read.

No helper edits or API changes have been made.

### 4. Other paths to audit/exercise

- `recordBackgroundPass`: shared draw image is discarded each frame via `transitionImageForComputeWrite` (source top-of-pipe/no access → compute-write).
- `recordGeometryPass`: depth image is similarly discarded; compute background → color attachment transition.
- `recordTonemapPass`: color-output writes → compute sampled reads; shared postprocessing image discarded before compute writes.
- Shared render-target cross-frame hazards have **not** been established or ruled out; inspect execution dependencies, not only layouts. Discarding contents does not itself remove synchronization obligations.
- `src/app/application.cpp`: SDL resize sets `resizeRequested`; minimized rendering is skipped until restored; cleanup calls engine cleanup.
- `VulkanEngine::run` (`src/vk_engine.cpp:689`): recreates if requested, builds UI, updates scene, draws.
- `src/app/ui.cpp:40–46`: File → Load opens a native file dialog and calls `loadModel`, then inserts/replaces a loaded scene. Exercise uploads and replacement/resource lifetime during active rendering.
- Present-mode UI, background controls, render scale, texture/material uploads, and normal shutdown remain to be exercised after fixes.

## Tools and local build environment

Previously observed Debug configuration (inspect current cache before use):

```text
Build directory: cmake-build-debug-visual-studio
CMAKE_BUILD_TYPE: Debug
Compiler: C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe
Developer environment: C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat
CMake: C:\Program Files\CMake\bin\cmake.exe (3.28.1)
Ninja: C:/Users/jpabl/AppData/Local/Programs/CLion/bin/ninja/win/x64/ninja.exe
Vulkan SDK: C:/VulkanSDK/1.4.350.0
```

Capability query was attempted with `Bin/vulkaninfo.exe --summary`, but that filename does not exist. A subsequent actual glob found:

```text
C:/VulkanSDK/1.4.350.0/Bin/vulkaninfoSDK.exe
C:/VulkanSDK/1.4.350.0/Bin/vulkanCapsViewer.exe
```

**The correct capability executable has not yet been run.** Query device/driver/extension/features using `vulkaninfoSDK.exe` rather than repeating the failed filename. No GPU capability conclusions have been drawn.

For future temporary `.cmd` scripts, quote Windows paths when invoking through the shell, e.g. `cmd.exe /c 'out\check.cmd'`. Avoid interpreting path separators as shell escapes.

The local default scene is an untracked reactor asset at `assets/structure.glb`; preserve it. Prior observed size 35,203,964 bytes and SHA256 `0b627ae72f7955d9c130c0654df2ff55ae2a524519acd8bfaeb818fec5c131c1`. The packaged CC0 scene is separate; verify both relevant rendering paths if needed without overwriting local assets.

## Continuation plan

1. Continue on `fix/vulkan-validation-errors`; inspect fresh source and current device capabilities. Finish tracing synchronization/resource lifetime through LSP, including uploads/deletion and presentation retirement.
2. Correct the acquire/present synchronization scopes, actual swapchain retirement, and the mipmap barrier mismatch. Fix any further errors revealed by the audit/runtime rather than suppressing them.
3. Run the actual Debug renderer using the existing harness:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/renderer-smoke.ps1 -Profile Debug
   ```

   It explicitly relinks the selected profile, captures stdout/stderr/screenshots, resizes and minimizes/restores, closes normally, preserves ImGui settings, and refuses an existing `demo.exe`. Read the generated report and **inspect actual screenshots**. Exit 0 alone is not visual proof; exit 3 means validation errors.
4. Add targeted interaction coverage beyond the harness: present-mode changes/repeated recreation, File → Load (including textured/mipmapped content and replacement if supported), background controls/render scale, and normal cleanup. Record exact exercised paths and untested cases. Do not promise universal clean validation across untested hardware.
5. Iterate any newly observed errors; duplicate limiting in the baseline may have concealed additional diagnostics. Keep environment/performance warnings distinct from errors.
6. After actual clean proof, update existing current documentation and release-note warnings. Do not erase historical failing evidence or prematurely claim clean validation.

### Documentation/release text to revisit after proof

Known-error statements were present in these files during preceding distribution work; read current contents before editing:

- `README.md` (around line 141).
- `packaging/README.txt` known limitations.
- `scripts/ci/publish-demo-release.ps1` generated release notes.
- `docs/DEMO_DISTRIBUTION_PLAN.md` remaining correctness work/status and historical validation evidence.
- `docs/VALIDATION_ERRORS_FIX_04_04_2026.md` is historical design context, not proof of current correctness.

Deliver concrete changes and evidence: build result, visual result, error count/identifiers, remaining warning categories, exercised lifecycle paths, evidence directory, and coverage limits. The user has not requested a PR for this validation task yet.
