vk-playground - Windows x64 demo

RUNNING
Extract the entire ZIP to a normal folder, then run demo.exe. Keep the DLLs,
assets/, spirv/ and fonts/ folders beside the executable. Do not run from inside the ZIP.
No Visual Studio, Vulkan SDK, Slang compiler or vcpkg installation is required.
The executable is unsigned; Windows may display reputation warnings.

REQUIREMENTS
Windows 10/11 x64 with an up-to-date GPU driver providing Vulkan 1.3,
VK_KHR_push_descriptor, dynamic rendering, synchronization2, buffer device
addresses, runtime descriptor arrays, partially bound sampled-image descriptors,
sampled-image update-after-bind, timeline semaphores and shader draw parameters.
VK_EXT_swapchain_maintenance1 with swapchainMaintenance1 is required for safe
presentation retirement, together with VK_EXT_surface_maintenance1 and
VK_KHR_get_surface_capabilities2 instance extensions.
Vulkan 1.3 support alone does not guarantee all required device features.
The Windows Vulkan loader and vendor driver are system prerequisites; this
archive does not replace them. Microsoft release C++ runtime DLLs are included
for app-local deployment. The Universal CRT is supplied by supported Windows.

OPTIONAL SAMPLE
The editor starts empty, with an XZ grid (one-unit squares) and colored XYZ axes.
No model assets are needed to launch. The package includes Polygonal Mind's CC0
Tomb Chaser 1 Layout Floor 01 at assets/structure.glb for File -> Load.
Its legacy placement parent node is retained; navigate the camera after loading.
Geometry and textures are unchanged. Source revision, license and checksums
accompany the package.

CONTROLS
W/A/S/D: move (default 2 units/second). Right mouse drag in the scene area: look.
Camera -> Move speed: adjust travel speed; Ctrl+click the slider to type a value.
File -> Load: open additional models (OBJ, FBX, glTF and other supported formats).
Stats: timings and present mode. Background: effect and lighting controls.
View -> Grid / Axes: toggle editor guides. One world unit represents one metre;
imports retain their source scale. Guides are not scene objects.
Select a Scene tree node to attach its object gizmo. Gizmo -> Translate / Rotate /
Scale chooses the tool; Local axes switches space (scaling is always local).
Left-drag object handles to edit. Changes are session-only, not saved to files.
The top-right axis navigator shows camera orientation: click an axis to align,
or hold and drag any axis dot or the gray sphere to orbit around the selected node
or world origin. Releasing an orbit drag does not snap back to an axis.
The editor uses bundled Noto Sans Regular at 18 px; no font installation is needed.

BUILD IDENTITY
See build-info.txt for the version, commit, working-tree state and configuration.
A dirty working tree means the archive is not identified by the commit alone.
See the accompanying .zip.sha256 file to verify the downloaded archive.

KNOWN LIMITATIONS
An optimized build does not run Vulkan validation and is not proof of correctness.
Local Debug checks on 2026-10-31 (RTX 5070 Ti) reported zero validation errors
after synchronization/presentation-retirement fixes, including resize, present-mode
changes, model import/replacement and normal shutdown. External layer-name and
small-allocation performance warnings remain. Other GPUs/drivers are unverified.
Build/package success does not mean the archive was GPU-tested.

LICENSES
The application license is LICENSE.txt. Third-party and demo-scene notices are
in licenses/; the bundled Noto Sans font license is fonts/OFL.txt.
Microsoft runtime distribution is subject to the applicable Visual
Studio license terms and redistributable-code list:
https://aka.ms/vs/18/redistribution
Do not redistribute debug C++ runtime libraries.
