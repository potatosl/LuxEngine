# All build dependencies must be installed before configuring LuxEngine.
# SDL3 provides SDL3::SDL3 through its CMake package on Windows, macOS, and Linux.
find_package(SDL3 CONFIG REQUIRED)
