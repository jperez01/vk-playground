---
description: CLion MCP xdebug/LLDB CPU debugging workflow, verified setup, and known validation caveats for vk-playground.
---

# CLion debugger (verified 2026-10-31)

- Use CLion MCP `xdebug_*` tools for CPU debugging. **Debug-Visual Studio + LLDB** was verified with the MSVC toolchain; CLion marked the selected GDB profile incompatible. Recheck the live profile and debugger availability each session; do not assume the IDE selection has persisted.
- Verified: launch `demo`, hit a temporary breakpoint in `Application::run()`, inspect stack/fields, evaluate a side-effect-free expression, step over, resume rendering, and close the window normally with exit 0. This is debugger proof, not Vulkan validation proof.
- Workflow: `xdebug_get_debugger_status` and `xdebug_list_breakpoints` first; check `get_compiler_info` / `get_run_configurations`; set a temporary breakpoint with `xdebug_set_breakpoint`; launch with `xdebug_start_debugger_session`; use `xdebug_control_session(WAIT_FOR_PAUSE)`, `xdebug_get_stack`, `xdebug_get_frame_values`, and `xdebug_evaluate_expression`. Use `STEP_OVER` / `RESUME` as needed.
- Use the returned session ID and fresh stack-frame indices after each step/pause. Override the working directory to the repository's `bin/` only when the configuration reports dynamic launch overrides supported. Preserve user breakpoints, clean up only your own breakpoint/session, and close normally to exercise cleanup.
- The verified run still reported `PRESENT_AFTER_WRITE`, `VUID-vkDestroySemaphore-semaphore-05149`, and `VUID-vkDestroySwapchainKHR-swapchain-01282`. Never treat successful debugging or exit 0 as a clean validation result.
