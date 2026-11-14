# Installs LuxEngine build dependencies (vcpkg packages + Jolt Physics 5.6.0)
param(
    [string]$DepsDir = "C:\LuxDeps",
    [string]$VcpkgDir = "C:\vcpkg",
    [switch]$IncludeEditorDeps
)

. (Join-Path $PSScriptRoot "Utf8.ps1")
$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName

function Run($name, [scriptblock]$cmd) {
    Write-Host "`n=== STEP: $name ==="
    & $cmd
    if ($LASTEXITCODE -ne 0) {
        throw "Step [$name] failed with exit code $LASTEXITCODE"
    }
}

# Locate cmake
$vsCmake = "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$cmake = if (Test-Path "$root/tools/cmake/bin/cmake.exe") { "$root/tools/cmake/bin/cmake.exe" } elseif (Test-Path $vsCmake) { $vsCmake } elseif (Get-Command cmake -ErrorAction SilentlyContinue) { (Get-Command cmake).Source } else { "cmake.exe" }

# Locate or bootstrap vcpkg
if (-not (Test-Path "$VcpkgDir\.git")) {
    if ($env:VCPKG_INSTALLATION_ROOT -and (Test-Path "$env:VCPKG_INSTALLATION_ROOT")) {
        $VcpkgDir = $env:VCPKG_INSTALLATION_ROOT
    } else {
        Run "clone vcpkg" { git clone --depth 1 https://github.com/microsoft/vcpkg $VcpkgDir }
    }
}
$vcpkgExe = Join-Path $VcpkgDir "vcpkg.exe"
if (-not (Test-Path $vcpkgExe)) {
    Run "bootstrap vcpkg" { & (Join-Path $VcpkgDir "bootstrap-vcpkg.bat") -disableMetrics }
}

# Install core vcpkg dependencies
$vcpkgPackages = @("sdl3", "stb", "vulkan-memory-allocator", "nlohmann-json")
if ($IncludeEditorDeps) {
    $vcpkgPackages += @("libzip", "qtbase[widgets]")
}
Run "vcpkg install $($vcpkgPackages -join ' ')" {
    & $vcpkgExe install --triplet x64-windows --clean-after-build @vcpkgPackages
}

# Build and install Jolt Physics 5.6.0
if (-not (Test-Path "$DepsDir\src\jolt")) {
    New-Item -ItemType Directory -Path "$DepsDir\src" -Force | Out-Null
    Run "clone Jolt 5.6.0" { git clone --depth 1 -b v5.6.0 https://github.com/jrouwe/JoltPhysics "$DepsDir\src\jolt" }
}

$genArgs = @()
if (Test-Path "C:/Program Files/Microsoft Visual Studio/18") {
    $genArgs = @("-G", "Visual Studio 18 2026", "-A", "x64")
} elseif (Test-Path "C:/Program Files/Microsoft Visual Studio/2022") {
    $genArgs = @("-G", "Visual Studio 17 2022", "-A", "x64")
}

Run "configure jolt" {
    & $cmake -S "$DepsDir\src\jolt\Build" -B "$DepsDir\build\jolt" @genArgs -DTARGET_UNIT_TESTS=OFF -DTARGET_HELLO_WORLD=OFF -DTARGET_SAMPLES=OFF -DTARGET_VIEWER=OFF -DTARGET_PERFORMANCE_TEST=OFF -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF -DINTERPROCEDURAL_OPTIMIZATION=OFF -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF -DPROFILER_IN_DEBUG_AND_RELEASE=OFF -DJPH_USE_DX12=OFF -DJPH_USE_VK=OFF -DJPH_USE_MTL=OFF -DJPH_USE_CPU_COMPUTE=OFF -DCMAKE_INSTALL_PREFIX="$DepsDir" -DCMAKE_DEBUG_POSTFIX=d
}

foreach ($cfg in "Release", "Debug") {
    Run "build jolt $cfg" { & $cmake --build "$DepsDir\build\jolt" --config $cfg -- /m /v:minimal }
    Run "install jolt $cfg" { & $cmake --install "$DepsDir\build\jolt" --config $cfg }
}

Write-Host "`nAll LuxEngine dependencies installed successfully."
