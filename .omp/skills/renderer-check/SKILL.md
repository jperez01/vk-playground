---
name: renderer-check
description: Build and smoke-check vk-playground renderer changes with Debug Vulkan validation, visual inspection, resize/minimize/restore, normal shutdown, and explicit evidence. Use for rendering, shaders, resources, synchronization, swapchain, loading, or UI changes.
---

# Renderer check

Run commands from the repository root unless a working directory is specified. Repository paths below are root-relative, not relative to this skill directory. Read `.omp/AGENTS.md` and `README.md` for project context. User arguments identify the changed feature or scenario; keep verification focused on it.

## 1. Choose the check

- Use **Debug** for correctness: `src/vk_engine.cpp` enables validation without `NDEBUG`, including best-practices and synchronization validation.
- Use **RelWithDebInfo** only for additional optimized/performance checks. It does not prove validation cleanliness.
- Before a renderer bug fix, capture the failing scenario if not already supplied by the user; repeat the same scenario after the fix. Never rerun a user-reported failure just to confirm it.
- Check `assets/structure.glb`, the selected CMake cache, Vulkan SDK, and build tools. Do not fabricate assets, change toolchains, or disable validation to bypass missing prerequisites.

## 2. Build and automate the lifecycle check

Prefer the repository harness from a normal Windows PowerShell terminal; it initializes the cached MSVC toolchain itself:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/renderer-smoke.ps1 -Profile Debug
```

The harness builds and relinks the selected profile, captures build/renderer stdout and stderr, shrinks/expands/minimizes/restores its own window, saves four screenshots, and closes normally. It preserves `bin/imgui.ini`, refuses existing `demo.exe` processes, and serializes other harness runs. This does not coordinate external IDE builds; do not start one concurrently.

Read the printed evidence directory's `report.json` and logs, then inspect the PNGs. Exit codes: `0` automated checks passed (visual review still required), `1` prerequisite/build failure, `2` runtime/capture/cleanup failure, `3` validation errors despite successful runtime checks. RelWithDebInfo validation is `not_checked`, never a validation pass. Warning counts include environment and performance messages; inspect the logs to classify them.

Use a new `-OutputDirectory` to keep named evidence, `-VcVarsAll` if MSVC setup cannot be derived from the compiler cache, or `-SettleSeconds` to allow slower rendering. Requires an interactive Windows desktop and an existing selected CMake build directory; no Python packages are needed. Retain harness evidence until it has been reviewed.

For feature-specific interactions or when diagnosing a harness failure, use the manual workflow below. Do not build or repeat the automatic lifecycle check unnecessarily after a successful harness run.

Manual build in an x64 Visual Studio developer shell, in `cmd.exe` from the repository root:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake --build cmake-build-debug-visual-studio --target demo
```

`build.bat` wraps the same Debug workflow with local absolute paths. On another machine, resolve its installed toolchain instead of copying those paths blindly. If a command fails, stop that command sequence and inspect the failure before launching anything.

The `demo` target builds its `Shaders` dependency and copies runtime DLLs. Both CMake profiles share `bin/demo.exe` and dependency destinations. When switching profiles, with no renderer running, remove only the generated `bin/demo.exe` before building the selected profile to force relinking and its post-build DLL copy; otherwise Ninja can consider the other profile's newer executable up to date. Do not delete user files or a running executable. Do not build the profiles concurrently.

If the build directory is absent, follow README setup using the installed vcpkg toolchain and add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` for native clangd. Do not overwrite an existing CMake cache with guessed options.

## 3. Launch and observe

For a manual or additional feature-specific check, launch the just-built executable with `bin/` as its working directory:

```bat
cd bin
demo.exe
```

Capture **both stdout and stderr** to fresh temporary logs or an IDE console, continuously through exit. Validation messages can appear on stdout, and repeated diagnostics can hit the layer's duplicate-message limit. Do not rely on the old `bin/output.log` or on process exit status alone. Use CLion MCP only if available and its selected build profile is confirmed; otherwise launch the executable directly.

- Confirm the `Vulkan Engine` window renders the default structure scene and ImGui **Stats** / **background** panels. Inspect a screenshot or the actual surface; a live process is insufficient.
- Let several frames render. Resize the window, minimize, and restore it; confirm the scene and UI resume rendering. The harness automates these window operations, but its report cannot judge the captured images.
- Exercise the changed path, not just startup. Examples: camera movement (`W/A/S/D`, mouse), **File → Load** for import changes, background effect controls for compute changes, or present-mode selection for swapchain changes. Only claim interactions actually performed.
- Close the window normally so `Application::cleanup()` runs. Record the exit code and diagnostics through shutdown. Termination/timeout is not a successful cleanup check. Stop only the instance started for this check; leave pre-existing user instances alone.
- If desktop interaction is unavailable, report that limitation and the exact unexercised scenarios. Do not replace visual proof with source inspection or a bare process-alive check.

## 4. Judge and report

Report build profile/result, scene and feature exercised, visual evidence, lifecycle checks, exit status, and diagnostics. Separate **build**, **runtime/visual**, and **validation** outcomes. A renderer that displays frames and exits 0 can still fail validation.

- Include validation errors/VUIDs or hazard names and relevant warnings. Distinguish environment/overlay warnings from renderer diagnostics.
- A pre-existing failure is still a failure, not an allowed baseline. For unrelated checks, report it without expanding the task into renderer fixes. For a targeted fix, compare the relevant before/after diagnostics.
- For performance work, additionally rebuild RelWithDebInfo and compare Stats CPU/per-pass GPU timings with the same scene, camera, resolution, present mode, and warmup. Debug timings are not an optimized performance baseline.
- Remove temporary scripts/logs after preserving needed evidence; leave no smoke process running. Avoid changing source code, saved UI preferences, or validation settings merely to run this skill.

## Observed baseline (2026-10-31; re-evaluate, do not suppress)

Debug build succeeded; the default scene and Stats/background UI rendered, resize and minimize/restore resumed rendering, and normal window close exited 0. Validation was **not clean**: `PRESENT_AFTER_WRITE` hazards from `vkQueuePresentKHR`, in-use semaphore destruction (`VUID-vkDestroySemaphore-semaphore-05149`), and in-use swapchain destruction (`VUID-vkDestroySwapchainKHR-swapchain-01282`). Warnings included transfer-write access with `TRANSFER_SRC_OPTIMAL`, dedicated small allocations, and Galaxy overlay layer naming. This records observed evidence, not exemptions or a guarantee about later runs.
