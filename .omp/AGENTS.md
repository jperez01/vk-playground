# vk-playground project brief

Windows C++20 / Vulkan 1.3 renderer using SDL2, Dear ImGui, Slang shaders, and CMake with the MSVC toolchain. Start with `README.md`; consult `docs/` for design history, not as proof of current implementation.

## Code map

- `src/vk_engine.*`: Vulkan setup, swapchain, per-frame resources, render passes, and submission.
- `src/app/`: SDL event loop, camera updates, and editor UI; `src/main.cpp` owns startup and cleanup.
- `src/vk_loader.*`, `src/model/`, `src/scene.*`: glTF/Assimp import and scene representation.
- `src/gpu_profiler.*`: GPU timestamps and debug labels.
- `shared/`: resource/deletion helpers, descriptors, pipelines, image transitions, camera, and common types.
- `shaders/`: Slang sources. `demo` depends on the `Shaders` target, which generates SPIR-V into `spirv/`.

## Build and run

- Existing profiles: `cmake-build-debug-visual-studio` (Debug, validation enabled) and `cmake-build-relwithdebinfo-visual-studio` (optimized, validation disabled).
- In an x64 Visual Studio developer shell, from the repository root: `cmake --build cmake-build-debug-visual-studio --target demo`. `build.bat` also initializes the local MSVC environment and builds Debug.
- Both profiles output to the same `bin/`; verify the binary's profile before running. Close your own renderer instance before rebuilding.
- Launch `demo.exe` with `bin/` as the working directory. The default scene requires the untracked `assets/structure.glb`; dependencies are listed in `vcpkg.json`.
- For renderer changes, use `.omp/skills/renderer-check/SKILL.md`: build success alone is not runtime or validation proof. No command-line frame limit or automatic exit is implemented; close the window normally to exercise cleanup.
- `scripts/renderer-smoke.ps1 -Profile Debug` automates the build and window lifecycle, writes logs/screenshots/report to a fresh evidence directory, and preserves saved ImGui settings. Read the report and inspect screenshots; feature-specific interactions still need a targeted check.

## Tools and boundaries

- Native clangd uses the RelWithDebInfo compilation database through `.omp/lsp.json`. Use it for symbol navigation/references; Debug validation still requires a Debug executable.
- CLion MCP is separate and requires the IDE/project open. Inspect its active profile before running the `demo` configuration; do not assume it selects Debug.
- For synchronization, resource-lifetime, barrier, or swapchain reviews, use `.omp/skills/vulkan-review/SKILL.md`. Trace symbols/callers with clangd, report concrete evidence-backed findings, then use renderer-check for targeted Debug verification. Reviews do not authorize automatic fixes.
- Edit sources, not generated `bin/`, `spirv/`, build directories, or vcpkg outputs. Avoid unrelated changes to vendored `third_party/` code and local assets.
- Capture stdout and stderr through normal shutdown. Report validation errors and untested scenarios explicitly; never disable validation or suppress diagnostics to obtain a passing check.

## CLion debugger (verified 2026-10-31)

- Use CLion MCP `xdebug_*` tools for CPU debugging. **Debug-Visual Studio + LLDB** was verified with the MSVC toolchain; CLion marked the selected GDB profile incompatible. Recheck the live profile and debugger availability each session; do not assume the IDE selection has persisted.
- Verified: launch `demo`, hit a temporary breakpoint in `Application::run()`, inspect stack/fields, evaluate a side-effect-free expression, step over, resume rendering, and close the window normally with exit 0. This is debugger proof, not Vulkan validation proof.
- Workflow: `xdebug_get_debugger_status` and `xdebug_list_breakpoints` first; check `get_compiler_info` / `get_run_configurations`; set a temporary breakpoint with `xdebug_set_breakpoint`; launch with `xdebug_start_debugger_session`; use `xdebug_control_session(WAIT_FOR_PAUSE)`, `xdebug_get_stack`, `xdebug_get_frame_values`, and `xdebug_evaluate_expression`. Use `STEP_OVER` / `RESUME` as needed.
- Use the returned session ID and fresh stack-frame indices after each step/pause. Override the working directory to the repository's `bin/` only when the configuration reports dynamic launch overrides supported. Preserve user breakpoints, clean up only your own breakpoint/session, and close normally to exercise cleanup.
- The verified run still reported `PRESENT_AFTER_WRITE`, `VUID-vkDestroySemaphore-semaphore-05149`, and `VUID-vkDestroySwapchainKHR-swapchain-01282`. Never treat successful debugging or exit 0 as a clean validation result.
