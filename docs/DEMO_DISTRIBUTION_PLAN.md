# Windows demo distribution plan

Status: steps 1 and 2 implemented; GitHub automation and release publication remain.
Last updated: 2026-10-31.

## Goal and conventions

Let someone download a Windows x64 ZIP, extract it, and run the renderer without a source checkout, Visual Studio, vcpkg, Slang, or the Vulkan SDK. A compatible GPU and Vulkan driver remain required.

Use two channels:

| Channel | Trigger | Download location | Meaning |
|---|---|---|---|
| Development | Successful default-branch build after a merge | GitHub Actions artifact | Current development snapshot; not necessarily GPU-tested |
| Demo release | Version tag, followed by package verification | GitHub Release asset | Fixed version deliberately published for users |

Build PRs before merge, but publish release assets only from trusted branch/tag events. A rolling public development prerelease is optional, not part of the initial implementation. The default branch is `master`.

Local and CI packaging must use the same command. Compilation, actual rendering, and Vulkan validation are separate outcomes. A successful optimized build must never be described as validation-clean solely because it runs.

## Step 1 — portable runtime resources: completed

Implemented on `feat/portable-runtime-paths`, together with step 2.

- `shared/runtime_paths.h/.cpp`: cache SDL's executable directory, free the SDL-owned string, and resolve resource paths with `std::filesystem` independently of the working directory.
- `shared/vk_pipelines.cpp`: load shaders from the executable's `spirv/` directory; report resolved missing paths to stderr.
- `src/vk_engine.cpp`: load `assets/structure.glb` beside the executable; check the default scene and generated shader list before creating GPU resources. Replace the scene assertion with an explicit load error and cleanup of initialized renderer subsystems.
- `src/main.cpp`: report application exceptions to stderr and return exit code 1.
- CMake: remove embedded checkout paths, derive required shader names from build outputs, and stage scene/shaders even when an incremental build does not relink. Raise minimum CMake to 3.21.

Acceptance exercised: relocated runtime outside the checkout, unrelated working directory, rendered scene/UI, normal exit 0, missing scene/shader and invalid scene diagnostics, and incremental staging with matching source/output hashes.

## Step 2 — reproducible packaging procedure: completed

### Installation and command

- `cmake/DemoInstall.cmake`: define the `Demo` install component, validate scene checksums/configuration, install the executable and required DLL closure, omit the system Vulkan loader/Windows DLLs, and include app-local release MSVC CRT files from the compiler installation's redistribution directory.
- `cmake/DemoNotices.cmake` and `packaging/licenses/`: collect vendored and vcpkg license notices, preserve relevant Vulkan SDK notices, and include scene attribution/CC0 terms and Microsoft runtime guidance.
- `scripts/package-demo.ps1`: use an existing configured Windows x64 MSVC build tree, prepare the licensed asset, force the selected target executable to relink, build and install only `Demo`, validate staged inputs, then generate ZIP/checksum output. Support single- and multi-configuration trees. Refuse Debug packages, existing output archives, running renderers, and simultaneous package/smoke operations.
- `scripts/prepare-demo-scene.ps1`: add a placement parent node to the downloaded GLB. Preserve original nodes, geometry, materials, textures and binary chunks.
- `packaging/README.txt`: ship run instructions, hardware requirements, controls, known limitations and licensing information.
- `README.md`: document local packaging, scene selection, output locations and verification obligations.

Run from the repository root after configuring the build tree:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-demo.ps1 -Version v0.1.0
# Or select another configured build tree:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-demo.ps1 -BuildDirectory build -Version v0.1.0
```

The default tree is `cmake-build-relwithdebinfo-visual-studio`. Outputs are under `out/packages/`: a versioned ZIP, `.zip.sha256`, and build/install logs in a printed `.work-*` directory. Without `-Version`, the archive label includes the Git commit and a dirty-tree suffix when applicable. Generated archives, build outputs and downloaded assets are not committed.

The procedure is repeatable, not a bit-for-bit reproducible-build guarantee: timestamps, local toolchain selection and uncommitted sources can change bytes. `build-info.txt` identifies the commit, dirty state, configuration and source/prepared scene checksums.

### Asset decision

The original reactor `structure.glb` exists in the Vulkan Guide `all-chapters-2` branch, but model-specific redistribution permission was not established. The nearby Lost Empire/Minecraft notice describes a different model and must not be applied to the reactor.

The user approved a licensed replacement for packages. `packaging/demo-scene.json` pins Polygonal Mind's CC0 **Tomb Chaser 1 — Layout Floor 01** GLB conversion, immutable revision, source SHA256 and permission evidence. A parent translation `[30, -12, -150]` puts it in front of the existing starting camera; the prepared file retains the renderer's `assets/structure.glb` filename.

The download and prepared scene live in the selected build tree's `demo-assets/`. The original local `assets/structure.glb` is not overwritten; its unchanged SHA256 was verified. Packaging sets that tree's `DEMO_SCENE_FILE`, source-file and prepared-checksum cache entries. Subsequent builds in the same tree retain the packaged scene until explicitly reconfigured.

### Evidence recorded on 2026-10-31

| Check | Outcome |
|---|---|
| Optimized single-config package | Passed |
| Fresh Ninja Multi-Config build and package, reusing installed vcpkg dependencies | Passed |
| Final ZIP extracted outside checkout, unrelated working directory, system-only PATH and SDK environment removed | Scene/UI rendered; normal exit 0 |
| Extracted-package resize and minimize/restore | Passed; screenshots inspected |
| ZIP checksum, scene hashes, required notices, excluded developer files | Verified |
| GLB preparation | Binary chunks and original nodes/geometry/materials unchanged; placement parent verified |
| Wrong source checksum / missing compiled shader / Debug installation | Installation refused |
| Existing ZIP | Packaging refused overwrite |
| Debug licensed-scene build/runtime and lifecycle | Passed; screenshots inspected; normal exit 0 |
| Debug Vulkan validation | Failed: 18 reported errors, 23 warnings; duplicate-message limit reached |
| User's extracted-package trial | User reported that it worked |

Validation identifiers included `PRESENT_AFTER_WRITE`, `VUID-vkDestroySemaphore-semaphore-05149`, and `VUID-vkDestroySwapchainKHR-swapchain-01282`. Optimized runtime logs also contained Galaxy overlay layer-naming warnings. No diagnostics were suppressed, and optimized execution was not counted as validation proof.

A clean SDK-free Windows machine was not available; the extracted-package test removed development paths/environment on the existing workstation. PowerShell 5.1 was exercised; PowerShell 7, Visual Studio's multi-config generator, other GPUs/drivers and a clean-machine install remain unverified.

Local evidence directories under `%TEMP%`:
- `vk-demo-package-b5befff216084fe180993a9897f00669`: final extracted-package screenshots and runtime logs.
- `vk-playground-smoke-5ebde8a4e2b44098b8901944781fa1b2`: Debug licensed-scene report, screenshots and logs.
- `vk-package-verification-b746fdfbdcda4392bf109ed224e83dcf`: failure-check and Debug configuration logs.

These are workstation-local evidence, not repository files or durable CI links. Temporary smoke scripts/runtime copies were removed; logs/screenshots were retained.

## Step 3 — GitHub builds and publication: remaining

1. Add `.github/workflows/build-windows.yml` for PRs to `master`, pushes to `master`, and manual dispatch.
2. Pin the Windows runner image, action revisions, Vulkan SDK/Slang toolchain and vcpkg checkout. Retain the manifest's dependency baseline and cache dependencies with toolchain/manifest-sensitive keys.
3. Configure a fresh Windows x64 MSVC optimized tree without requiring the untracked local reactor asset. Fail configuration clearly if Slang is missing.
4. Invoke `scripts/package-demo.ps1`; upload the ZIP, checksum and useful logs with explicit artifact names and retention. Record source identity; never publish a partial package.
5. Keep PR jobs read-only with respect to release publication. Do not expose release-write credentials to untrusted PR code.
6. Add `.github/workflows/release-windows.yml` for version tags. Build the tagged commit through the same command and create a **draft** GitHub Release containing the ZIP/checksum, source identity and release notes. Grant write permission only to the publishing job.
7. Exercise the workflows on GitHub, including a PR build, default-branch artifact download, failure diagnostics and a tagged draft release. Local scripts alone are not proof that CI works.

Acceptance: each successful merge produces an identifiable development download; a version tag produces a draft release for a specific commit. No GPU-tested or validation-clean claim is inferred from hosted-runner build success.

## Step 4 — release acceptance and first publication: remaining

1. Extend the existing renderer smoke harness with an explicit extracted-package/executable mode so package checks reuse its capture and lifecycle logic instead of retaining ad-hoc scripts.
2. Exercise the GitHub-built archive on a GPU-equipped Windows desktop: launch outside the checkout with an unrelated working directory, inspect scene/UI, exercise camera and File -> Load, resize/minimize/restore, and close normally. Retain logs and screenshots.
3. Verify required DLLs on a clean Windows 10/11 x64 machine without development tools/SDK, with a supported GPU driver.
4. Run matching Debug Vulkan-validation checks separately. Report build, runtime/visual, and validation outcomes independently.
5. Add README download links and a release checklist, including hardware requirements, known issues and evidence for the exact release candidate. Publish the draft only after reviewing that candidate.

Existing synchronization/cleanup validation failures need a separately scoped renderer fix before any release is described as validation-clean. This packaging work neither fixes them nor grants an exemption.

## Deliberately outside the initial scope

- Automatic GPU validation on ordinary GitHub-hosted Windows runners.
- Installers, automatic updates, code signing, Linux/macOS distributions or storefronts.
- Rolling public prereleases unless anonymous development-download access becomes necessary.
- Redistributing the original reactor scene without established permission.
- A full renderer correctness/performance rewrite or bit-for-bit reproducible builds.
