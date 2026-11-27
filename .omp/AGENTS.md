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
- Launch `demo.exe` with `bin/` as the working directory. Startup is an empty editor workspace with a one-unit XZ grid and colored XYZ axes; no model assets are required. Keep `spirv/` and `fonts/` beside the executable. Dependencies are listed in `vcpkg.json`; import optional local models through **File → Load**.
- For renderer changes, use `.omp/skills/renderer-check/SKILL.md`: build success alone is not runtime or validation proof. No command-line frame limit or automatic exit is implemented; close the window normally to exercise cleanup.
- `scripts/renderer-smoke.ps1 -Profile Debug` automates the build and window lifecycle, writes logs/screenshots/report to a fresh evidence directory, and preserves saved ImGui settings. Read the report and inspect screenshots; feature-specific interactions still need a targeted check.

## Tools and boundaries

- Native clangd uses the RelWithDebInfo compilation database through `.omp/lsp.json`. Use it for symbol navigation/references; Debug validation still requires a Debug executable.
- CLion MCP is separate and requires the IDE/project open. Inspect its active profile before running the `demo` configuration; do not assume it selects Debug.
- For CLion MCP CPU debugging (`xdebug_*` / LLDB workflow and caveats), read `rule://clion-debugger`.
- For synchronization, resource-lifetime, barrier, or swapchain reviews, use `.omp/skills/vulkan-review/SKILL.md`. Trace symbols/callers with clangd, report concrete evidence-backed findings, then use renderer-check for targeted Debug verification. Reviews do not authorize automatic fixes.
- Edit sources, not generated `bin/`, `spirv/`, build directories, or vcpkg outputs. Avoid unrelated changes to vendored `third_party/` code and local assets.
- Capture stdout and stderr through normal shutdown. Report validation errors and untested scenarios explicitly; never disable validation or suppress diagnostics to obtain a passing check.
