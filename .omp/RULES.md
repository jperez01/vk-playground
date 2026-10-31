# Renderer correctness rules

1. Use Debug with Vulkan validation for renderer correctness checks; RelWithDebInfo is for additional performance checks, not validation proof.
2. Never suppress validation diagnostics or count known errors as a passing check. Report build, runtime/visual, and validation outcomes separately.
3. Exercise the changed path and normal cleanup using the renderer-check skill. Inspect actual rendering or screenshots; compilation and a live process are insufficient.
4. Use native LSP for symbol-aware navigation, references, and supported refactorings. Use text/file search for literals and filenames, not as a substitute for available semantic tools.
5. Never build profiles concurrently into shared bin/ outputs or overwrite a running renderer. Select the profile explicitly and force its executable relink when switching profiles; control only renderer instances you start.
