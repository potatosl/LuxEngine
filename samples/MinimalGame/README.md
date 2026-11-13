# MinimalGame — LuxEngine SDK Consumer Example

This example demonstrates how an external C++20 game project links against the installed LuxEngine SDK.

## Requirements
- CMake 3.24+
- C++20 compatible compiler (MSVC 2022+ / Clang 18+)
- LuxEngine SDK installed

## How to Build

```powershell
# Set LUX_ENGINE_DIR or pass -DLuxEngine_DIR
cmake -S . -B build -DLuxEngine_DIR="<path-to-sdk>/lib/cmake/LuxEngine"
cmake --build build --config Release
.\build\Release\MinimalGame.exe
```
