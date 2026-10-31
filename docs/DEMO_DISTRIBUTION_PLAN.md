# Windows demo distribution plan

Status: steps 1–3 implemented; hosted PR builds, artifact download/runtime and tagged draft release verified. Default-branch activation awaits merging the CI PR; first public release acceptance remains step 4.
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

A clean SDK-free Windows machine was not available; the extracted-package test removed development paths/environment on the existing workstation. Step 2 exercised PowerShell 5.1; subsequent hosted PowerShell 7 packaging evidence is recorded below. Visual Studio's multi-config generator, other GPUs/drivers and a clean-machine install remain unverified.

Local evidence directories under `%TEMP%`:
- `vk-demo-package-b5befff216084fe180993a9897f00669`: final extracted-package screenshots and runtime logs.
- `vk-playground-smoke-5ebde8a4e2b44098b8901944781fa1b2`: Debug licensed-scene report, screenshots and logs.
- `vk-package-verification-b746fdfbdcda4392bf109ed224e83dcf`: failure-check and Debug configuration logs.

These are workstation-local evidence, not repository files or durable CI links. Temporary smoke scripts/runtime copies were removed; logs/screenshots were retained.

## Step 3 — GitHub builds and publication: implemented and PR/tag verified

- `.github/workflows/build-windows.yml`: build PRs targeting `master`, pushes to `master`, and manual dispatches. Upload the development ZIP/checksum and separate diagnostics with 14-day retention.
- `.github/workflows/release-windows.yml`: build version tags, then create a draft release in a separate publishing job. Build jobs have `contents: read`; only the publisher gets `contents: write` and artifact-read permission.
- `.github/actions/package-windows/action.yml`: share toolchain provisioning, vcpkg binary caching, fresh optimized configuration, the existing packaging command, and artifact upload between both workflows. PRs build the merge checkout; tagged builds use the tagged commit. There is no `pull_request_target` execution of PR code.
- `scripts/ci/windows-toolchain.json` and `prepare-windows.ps1`: checksum-pin Vulkan SDK/Slang 1.4.350.0, CMake 3.31.6 and Ninja 1.13.2; pin vcpkg tooling to release 2026.10.16 while keeping dependency versions governed by the existing manifest baseline. The original 2024 tooling pin tried to download a retired MSYS2 pkgconf package; the maintained tooling fixes acquisition without changing the dependency baseline. Enforce the hosted VS2022 default MSVC 14.44 family and record the full selected compiler version in toolchain evidence/cache keys. `windows-2022` is a selected runner family, not an immutable image; MSVC patch updates are recorded rather than claimed to be bitwise pinned.
- SDK installation is copy-only under the runner's temporary directory. Downloaded executables/archives are hash-verified before use; no existing local SDK or untracked reactor asset is required.
- `scripts/ci/publish-demo-release.ps1`: verify package hash, expected archive/root names, clean-source build identity, tag version, configuration, architecture and remote tag target before draft creation. Handle annotated/lightweight tags, refuse an existing release, attach ZIP/checksum, mark prerelease versions appropriately, and never publish automatically.
- Root CMake configuration now fails early for a missing Slang compiler, including an invalid cached compiler path.

The vcpkg binary cache key includes the manifest/toolchain configuration, selected full MSVC version, SDK version, CMake version and pinned vcpkg revision. GitHub's branch/ref cache isolation keeps PR writes from becoming trusted default-branch caches. Configure, package and toolchain diagnostics are uploaded even when a preceding build step fails; only successful packaging uploads the distributable artifact.

Local checks exercised: actionlint 1.7.12 accepted both workflows; PowerShell parsing passed; `-VerifyOnly` accepted a local metadata fixture and rejected a tampered checksum and mismatched commit without GitHub mutations. A fresh CMake configuration with a nonexistent cached Slang path failed with the expected compiler diagnostic. These checks do not replace a real hosted build or a real release-artifact verification.

### Hosted verification on 2026-10-31

| Scenario | Evidence and result |
|---|---|
| Fresh PR build, dependency installation, ZIP/checksum and diagnostic upload | [Run 41245974051](https://github.com/jperez01/vk-playground/actions/runs/41245974051) passed |
| Subsequent PR build using the saved dependency cache | [Run 41246886768](https://github.com/jperez01/vk-playground/actions/runs/41246886768) passed; primary-key cache hit recorded |
| Download and execute an actual GitHub-built artifact | Verified SHA256 and clean merge-checkout identity `60ee7a8535b70a8925882bc145efb1a0ea509763`; scene/UI rendered outside the checkout with system-only PATH and SDK environment removed; resize/minimize/restore and normal exit 0 |
| Annotated version tag and draft-release publisher | [Run 41246883402](https://github.com/jperez01/vk-playground/actions/runs/41246883402) passed both jobs for commit `c4dd0c5d78b1045e3fbf7e99ada74c37cce68c52`; GitHub reported draft=true, prerelease=true and publishedAt=null, with ZIP/checksum attached |
| Independently download and verify draft assets | Actual downloaded archive passed the publisher's local identity/hash verification; ZIP SHA256 `8ce37c77fe16e12e34bf2ac22be7760816bdc3c2d22ec716a1ef7e305e50ff04` |
| Failure diagnostics | [Run 41245646703](https://github.com/jperez01/vk-playground/actions/runs/41245646703) failed during the retired-tool download issue and retained downloadable logs; later runs passed after the tooling fix |

The temporary verification drafts/tags were removed after inspection; no public release was published. Hosted provisioning recorded MSVC toolset `14.44.35207` / compiler `19.44.35229.0`, Vulkan SDK `1.4.350.0` / Slang `2026.8`, CMake `3.31.6` and Ninja `1.13.2`. Packaging and scene preparation ran under PowerShell 7; the draft publisher also ran successfully under the workflow's Windows PowerShell shell.

Actual artifact runtime screenshots/logs are retained locally under `%TEMP%/vk-github-artifact-d2f58dd0c79a4def8f4dad8e45fc662a`. Runtime logs contained the existing Galaxy overlay naming warnings. This optimized check did not run Vulkan validation, camera/model-import interactions, or a clean-machine install. The existing Debug validation failures above remain unresolved.

Default-branch push and manual-dispatch checks require the workflows to be merged into `master`; this change does not merge its own PR. Review and merge [PR #19](https://github.com/jperez01/vk-playground/pull/19), then inspect the resulting `master` build and manual-dispatch availability. These are explicit post-merge activation checks, not already exercised results. No GPU-tested or validation-clean claim follows from hosted-runner success alone.

## Step 4 — release acceptance and first publication: remaining

1. Extend the existing renderer smoke harness with an explicit extracted-package/executable mode so package checks reuse its capture and lifecycle logic instead of retaining ad-hoc scripts.
2. Exercise the chosen release candidate on a GPU-equipped Windows desktop, including camera and File -> Load, and retain logs/screenshots. Basic GitHub-artifact rendering and window lifecycle were already exercised above; repeat acceptance for the exact candidate intended for publication.
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
