---
name: vulkan-review
description: Review vk-playground synchronization, GPU resource ownership, barriers, frame-slot reuse, uploads, and swapchain recreation. Use for rendering/resource changes or explicit Vulkan correctness reviews; produce evidence-backed findings and targeted Debug validation, not automatic fixes.
---

# Vulkan review

Read-only by default: investigate and report; do not fix renderer code, alter validation settings, or change the user's IDE/debugger state unless asked. User arguments define the feature, change, or lifecycle to review. Repository paths below are relative to the repository root, not this skill directory.

## 1. Bound the review and gather evidence

- Read `.omp/AGENTS.md` and the relevant source, not just design-history documents. Respect `.omp/RULES.md`.
- Scope to the requested change plus its owners, callers, and GPU consumers. If no scope is supplied, review acquire/submit/present and resize/cleanup; state that upload/material paths were not covered. Do not silently expand into a renderer-wide rewrite or performance audit.
- Use native clangd definitions/references to trace relevant C++ symbols across files. Use text search for literals, diagnostic names, Vulkan flags, and shader source when semantic tooling is unavailable. If LSP fails, report the limitation rather than claiming complete caller coverage.
- Recheck function locations; the entry points below are a map, not permanent line numbers. clangd currently analyzes RelWithDebInfo; its results are not evidence of Debug validation.
- Distinguish observed code facts, current runtime evidence, and unconfirmed hypotheses. Do not repeat user-reported failures just to confirm them, or turn historical diagnostics into current findings without checking the relevant code.

## 2. Trace the ownership and dependency chain

For each in-scope resource or synchronization object, identify its creator/owner, indexing key, users, last-use submission, completion proof, and reuse/destruction path. A compact table is useful when several objects differ. Follow the actual queue and stage/access scopes; a nearby wait is not necessarily the required wait.

Current navigation starting points:

| Concern | Entry points to trace with clangd |
|---|---|
| Acquire and frame reuse | `VulkanEngine::beginFrame`, `getCurrentFrame`, `getCurrentFrameSlot`, `FrameData` |
| Queue submission and presentation | `VulkanEngine::draw`, `submitAndPresent`, `vkinit::semaphoreSubmitInfo` |
| Resize and shutdown | `createSwapchain`, `destroySwapchain`, `resizeSwapchain`, `cleanup`; SDL events in `src/app/application.cpp` |
| Pass dependencies and layouts | `recordBackgroundPass`, `recordGeometryPass`, `recordTonemapPass`, `recordBlitPass`, `recordUiPass`; `shared/vk_images.*` |
| Upload and deferred destruction | `UploadBatch::submitAndWait`, `DeletionQueue::setPendingValue`, `collect`, `flushAll`; RAII types in `shared/vk_types.h` and `shared/vk_resources.*` |

Apply only the checks relevant to the scope:

### Acquire -> render -> present

- Determine the first actual use of the acquired image (including copies/blits and layout transitions). Check that the acquire semaphore wait scope orders that use, not merely a later color attachment operation.
- Check the render-finished signal scope against all writes and the final transition to presentation. Verify that present waits on the intended binary semaphore, with no premature reuse or double signal/wait.
- Distinguish graphics-submit completion from presentation completion. A frame timeline/fence proving rendering finished does not by itself prove the presentation engine consumed its wait semaphore or released old swapchain resources. Identify the actual WSI completion/retirement mechanism; consult current Vulkan specification/guide requirements before prescribing one.
- Trace separate queue families if used: ownership transfers and presentation support are not implied by queue names. Do not invent a multi-queue problem in a single-queue path.

### Resource lifetime and recreation

- Check last GPU use before buffer/image/view/sampler/descriptor destruction or overwrite. Follow device-address and bindless references as well as direct handle uses.
- For deferred deletion, trace how the retirement value is assigned, signaled, collected, and flushed. Confirm it covers every relevant use and queue; check shutdown ordering and destruction of the device/allocator last.
- Trace both normal shutdown and swapchain recreation callers. A device-idle wait in cleanup does not protect a separate resize path.
- During recreation, check old swapchain image/view/semaphore retirement, replacement extent/format, dependent render targets, descriptor updates, and per-image layout bookkeeping. Distinguish swapchain-owned images from application-owned allocations.
- Check zero/minimized extents, out-of-date/suboptimal acquire/present results, and early-return paths for invalid image indices, skipped waits, unreused acquired images, or inconsistent frame/timeline state. Identify a concrete reachable failure before reporting one.

### Barriers and frame state

- For each relevant producer/consumer pair, compare actual operations to old/new layouts, stage/access masks, aspect/mip/layer ranges, and queue-family ownership. Review helper call sites, not only the helper implementation.
- Treat `UNDEFINED` as discarding prior contents, not a universal synchronization shortcut. Check depth/stencil and mip ranges only when the path uses them.
- Separate frame-slot indexing from acquired-image indexing. Verify command-pool/buffer, descriptor allocator, staging and mapped-data reuse against the completion proof for that object. Check assumptions about `FRAME_OVERLAP` versus swapchain image count.
- For uploads, trace host writes/flush requirements, copy visibility to consumers, staging lifetime, submit/wait ordering, and partial/error paths. Do not infer GPU completion from RAII scope exit or a CPU memcpy.

## 3. Exercise the affected path

Use `skill://renderer-check` for runtime verification; do not duplicate its build/launch procedure here. Default to its Debug smoke harness for resize/presentation/lifetime scope. Inspect the report, full stdout/stderr through normal close, and actual screenshots. Existing supplied evidence can support a read-only review when it matches the current source and scenario; label its provenance and limits. Do not rerun known failures merely for confirmation.

Add feature-specific interaction only when needed: load/unload for resource/import changes, background-effect controls for compute dependencies, or present-mode changes for that recreation path. The harness alone does not exercise those interactions. Do not infer an optimized performance result from Debug timings.

If a CPU-state question remains, use the verified CLion MCP debugger workflow in `.omp/AGENTS.md`; recheck Debug + LLDB and preserve user sessions/breakpoints. A CPU breakpoint does not prove GPU/WSI completion. If execution is unavailable, finish the source review and explicitly mark runtime/visual validation unverified.

## 4. Deliver findings, not a completed checklist

List findings first, highest severity first. Each finding needs:

- A concise title and severity (`P1` correctness/lifetime hazard, `P2` narrower failure, `P3` minor issue).
- Current `path:line` evidence and the owner/caller/consumer chain that makes it reachable.
- The triggering scenario and user-visible risk; avoid asserting a crash or corruption that was not observed.
- Evidence status: code-confirmed, runtime-confirmed (cite log/VUID/hazard), or hypothesis with its missing proof. A validation diagnostic identifies a hazard, not automatically its exact source-level cause.
- A concrete correction direction or smallest discriminating check, without implementing a fix unless requested.

Group repeated messages from one cause. Do not pad the report with style nits, generic best practices, or speculative findings. If no findings are supported, say so and name the coverage and untested paths; do not declare the whole renderer correct.

End with separate build, runtime/visual, and validation outcomes, plus blockers or residual risks. Preserve needed evidence, leave no process or temporary breakpoint behind, and keep pre-existing errors visible rather than counting them as passes.
