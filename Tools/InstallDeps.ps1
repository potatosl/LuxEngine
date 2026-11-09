# Installs LuxEngine build dependencies (vcpkg packages + Box2D 2.4.1)
param(
    [string]$DepsDir = "C:\LuxDeps",
    [string]$VcpkgDir = "C:\vcpkg",
    [switch]$IncludeEditorDeps
)

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
$cmake = if (Test-Path "$root/Tools/cmake/bin/cmake.exe") { "$root/Tools/cmake/bin/cmake.exe" } elseif (Test-Path $vsCmake) { $vsCmake } elseif (Get-Command cmake -ErrorAction SilentlyContinue) { (Get-Command cmake).Source } else { "cmake.exe" }

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
$vcpkgPackages = @("sdl3", "stb", "vulkan-memory-allocator")
if ($IncludeEditorDeps) {
    $vcpkgPackages += @("libzip", "qtbase[widgets]")
}
Run "vcpkg install $($vcpkgPackages -join ' ')" {
    & $vcpkgExe install --triplet x64-windows --clean-after-build @vcpkgPackages
}

# Build and install Box2D 2.4.1
if (-not (Test-Path "$DepsDir\src\box2d")) {
    New-Item -ItemType Directory -Path "$DepsDir\src" -Force | Out-Null
    Run "clone box2d 2.4.1" { git clone --depth 1 -b v2.4.1 https://github.com/erincatto/box2d "$DepsDir\src\box2d" }
}

$genArgs = @()
if (Test-Path "C:/Program Files/Microsoft Visual Studio/18") {
    $genArgs = @("-G", "Visual Studio 18 2026", "-A", "x64")
} elseif (Test-Path "C:/Program Files/Microsoft Visual Studio/2022") {
    $genArgs = @("-G", "Visual Studio 17 2022", "-A", "x64")
}

Run "configure box2d" {
    & $cmake -S "$DepsDir\src\box2d" -B "$DepsDir\build\box2d" @genArgs -DBOX2D_BUILD_UNIT_TESTS=OFF -DBOX2D_BUILD_TESTBED=OFF -DBOX2D_BUILD_DOCS=OFF -DCMAKE_INSTALL_PREFIX="$DepsDir" -DCMAKE_DEBUG_POSTFIX=d
}

foreach ($cfg in "Release", "Debug") {
    Run "build box2d $cfg" { & $cmake --build "$DepsDir\build\box2d" --config $cfg -- /m /v:minimal }
    Run "install box2d $cfg" { & $cmake --install "$DepsDir\build\box2d" --config $cfg }
}

Write-Host "`nAll LuxEngine dependencies installed successfully."
