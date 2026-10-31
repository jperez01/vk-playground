vk-playground - Windows x64 demo

RUNNING
Extract the entire ZIP to a normal folder, then run demo.exe. Keep the DLLs,
assets/ and spirv/ folders beside the executable. Do not run from inside the ZIP.
No Visual Studio, Vulkan SDK, Slang compiler or vcpkg installation is required.
The executable is unsigned; Windows may display reputation warnings.

REQUIREMENTS
Windows 10/11 x64 with an up-to-date GPU driver providing Vulkan 1.3,
VK_KHR_push_descriptor, dynamic rendering, synchronization2, buffer device
addresses, runtime descriptor arrays, partially bound sampled-image descriptors,
sampled-image update-after-bind, timeline semaphores and shader draw parameters.
Vulkan 1.3 support alone does not guarantee all required device features.
The Windows Vulkan loader and vendor driver are system prerequisites; this
archive does not replace them. Microsoft release C++ runtime DLLs are included
for app-local deployment. The Universal CRT is supplied by supported Windows.

DEMO SCENE
Polygonal Mind's CC0 Tomb Chaser 1 Layout Floor 01. A placement parent node
positions it for the existing camera; geometry and textures are unchanged.
Original source revision, license evidence and checksums accompany the package.

CONTROLS
W/A/S/D and mouse: move/look.
File -> Load: open additional models (OBJ, FBX, glTF and other supported formats).
Stats: timings and present mode. Background: effect and lighting controls.

BUILD IDENTITY
See build-info.txt for the version, commit, working-tree state and configuration.
A dirty working tree means the archive is not identified by the commit alone.
See the accompanying .zip.sha256 file to verify the downloaded archive.

KNOWN LIMITATIONS
An optimized build does not run Vulkan validation and is not proof of correctness.
Existing Debug validation checks report PRESENT_AFTER_WRITE presentation hazards,
VUID-vkDestroySemaphore-semaphore-05149 and
VUID-vkDestroySwapchainKHR-swapchain-01282 during swapchain cleanup.
Build/package success does not mean the archive was GPU-tested.

LICENSES
The application license is LICENSE.txt. Third-party and demo-scene notices are
in licenses/. Microsoft runtime distribution is subject to the applicable Visual
Studio license terms and redistributable-code list:
https://aka.ms/vs/18/redistribution
Do not redistribute debug C++ runtime libraries.
