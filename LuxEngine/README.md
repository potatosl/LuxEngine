# LuxEngine Starter — C++20, SDL3 GPU

A starter runtime for Windows, Linux and macOS. It creates an SDL3 window and clears the GPU swapchain with a slowly changing color. No engine editor, meshes, or sprites are included yet.

## Prerequisites

- CMake 3.24 or later
- C++20 compiler (MSVC, Clang or GCC)
- An installed SDL3 development package with `SDL3Config.cmake`
- A GPU and driver supported by SDL3 GPU API (Vulkan, D3D12 or Metal)
- On Linux, a suitable GPU driver may be required to run the demo.

## Build

```sh
cmake -S . -B build
cmake --build build --config Debug
```

Launch the binary in `build/bin` or `build/bin/Debug`, depending on generator/platform. Use Escape or close the window to exit.

### Windows (Visual Studio generator)

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
.\build\bin\Debug\LuxEngineDemo.exe
```

This project uses the installed SDL3 CMake package and does not download dependencies during configuration or build. If SDL3 is in a custom location, pass `-DSDL3_DIR=/path/to/SDL3Config.cmake/directory` to CMake.

## Architecture

- Engine: application loop and first SDL3 GPU renderer implementation
- Runtime: sample program executable
- Tests: placeholder
- Assets/Shaders: future graphics pipelines and shader assets

## Next work

1. Separate GPU commands from the engine renderer interface.
2. Introduce RAII GPU handles and texture/buffer ownership.
3. Add shader compilation/pipeline management.
4. Add 2D quad with vertex and index buffers.
5. Add 3D camera and depth-buffered triangle/mesh.
6. Introduce EnTT scene and component systems.

Note: GPU initialization requires a compatible driver. These source files were generated and reviewed against SDL3 documentation; actual compilation across all 3 target operating systems was not performed in this environment.
