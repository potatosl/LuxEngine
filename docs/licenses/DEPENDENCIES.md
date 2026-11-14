# LuxEngine Dependency Manifest & Licensing Audit

This document details all third-party software components bundled or linked with LuxEngine, LuxEditor, and LuxPlayer.

| Component | Version | License | Linkage | Purpose | Redistribution Notice |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **SDL3** | 3.2.x | zlib | Shared (`SDL3.dll`) | Windowing, Input, Audio, Event Loop | See `LICENSE-SDL3.txt` |
| **Jolt Physics** | 5.6.0 | MIT | Static (`Jolt.lib` / `libJolt.a`) | 3D rigid bodies and constrained XY-plane 2D simulation | See `LICENSE-Jolt.txt`; official revision `e77f175595e64cb44218cc9d9d56fc365ad0e36a` |
| **Qt 6** | 6.x | LGPLv3 | Shared (`Qt6*.dll`) | LuxEditor Desktop UI Framework | See `LICENSE-Qt6.txt` & `LICENSE-LGPLv3.txt` |
| **Vulkan Memory Allocator (VMA)** | 3.x | MIT | Header / Source | Vulkan GPU Memory Management | See `LICENSE-VMA.txt` |
| **Khronos Vulkan Loader** | 1.3.x | Apache-2.0 | Dynamic (`vulkan-1.dll` / System) | Vulkan Driver Dispatcher | See `LICENSE-VulkanLoader.txt` |
| **libzip** | 1.10.x | 3-Clause BSD | Shared (`zip.dll`) | Project Archive & Game Packaging | See `LICENSE-libzip.txt` |
| **stb_image** | 2.30 | Public Domain / MIT | Header / Source | Texture Image Decoding (PNG, JPG) | See `LICENSE-stb_image.txt` |
| **nlohmann/json** | 3.11+ | MIT | Header / Source | Scene and Project JSON | See `LICENSE-nlohmann-json.txt` |
| **ufbx** | 0.23.1 | MIT (Alternative A) | Private C object in LuxEngine | ASCII / binary FBX mesh import | See `LICENSE-ufbx.txt`; pinned commit and hashes in `Engine/ThirdParty/ufbx/SourceManifest.json` |
| **Kenney Prototype Kit** | 1.0 | CC0-1.0 | FBX / texture resources | Basic 3D model library | Original `License.txt` and source/file hashes in `Engine/Resources/Models/Primitives/KenneyPrototype/Provenance.json` |
| **DirectX Shader Compiler (DXC)** | 1.8.x | Apache-2.0 with LLVM Exception | Build Tool | HLSL to SPIR-V Compilation | Build tool only (not bundled in runtime) |

## LGPLv3 Compliance Notice for Qt 6
- **Dynamic Linking**: Qt 6 libraries are linked dynamically (`Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `platforms/qwindows.dll`).
- **User Library Replacement**: End-users can replace these shared libraries with compatible versions without rebuilding LuxEditor.
- **Source Code Availability**: Full Qt source code is accessible at https://download.qt.io/ and https://code.qt.io/.
- **Full License Text**: The complete verbatim texts of GNU LGPLv3 and GNU GPLv3 are bundled in `licenses/LICENSE-LGPLv3.txt`.
