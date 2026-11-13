# Package LuxEngine SDK into a portable ZIP distribution
param(
    [string]$BuildDir = "build/local",
    [string]$OutputDir = "dist",
    [string]$Version = "0.1.0"
)

. (Join-Path $PSScriptRoot "Utf8.ps1")
. (Join-Path $PSScriptRoot "WindowsRuntime.ps1")
$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

Write-Host "=== Packaging LuxEngine SDK (v$Version) ==="
$outDirResolved = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $root $OutputDir }
$sdkStaging = Join-Path $outDirResolved "LuxEngine-SDK-$Version-x64"
$sdkZip = Join-Path $outDirResolved "LuxEngine-SDK-$Version-x64.zip"

if (Test-Path $sdkStaging) {
    Remove-Item -Recurse -Force $sdkStaging
}
if (Test-Path $sdkZip) {
    Remove-Item -Force $sdkZip
}
New-Item -ItemType Directory -Path $sdkStaging -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $sdkZip) -Force | Out-Null

# Keep both suffixed engine libraries, then leave shared runtime files in Release mode.
Write-Host "Installing Debug SDK to staging..."
& cmake --install $BuildDir --config Debug --prefix $sdkStaging
if ($LASTEXITCODE -ne 0) { throw "cmake --install Debug failed" }

Write-Host "Installing Release SDK to staging..."
& cmake --install $BuildDir --config Release --prefix $sdkStaging
if ($LASTEXITCODE -ne 0) { throw "cmake --install Release failed" }

# Copy SDL3 runtime DLL into bin from Release build
$buildDirResolved = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$sdlDll = Join-Path $buildDirResolved "bin/Release/SDL3.dll"
if (Test-Path $sdlDll) {
    Copy-Item -Path $sdlDll -Destination "$sdkStaging/bin/SDL3.dll" -Force
    Write-Host "Copied SDL3.dll to SDK bin/"
} else {
    throw "Packaging failure: Release SDL3.dll not found in $BuildDir/bin/Release. Build Release first."
}

# Bundle Box2D headers, libraries and CMake configuration
$box2dInclude = "C:/LuxDeps/include/box2d"
if (Test-Path $box2dInclude) {
    New-Item -ItemType Directory -Path "$sdkStaging/include/box2d" -Force | Out-Null
    Copy-Item -Path "$box2dInclude/*" -Destination "$sdkStaging/include/box2d" -Recurse -Force
    Write-Host "Bundled Box2D headers"
}
$box2dLibRelease = "C:/LuxDeps/lib/box2d.lib"
if (Test-Path $box2dLibRelease) {
    Copy-Item -Path $box2dLibRelease -Destination "$sdkStaging/lib/box2d.lib" -Force
    Write-Host "Bundled Box2D Release lib"
}
$box2dLibDebug = "C:/LuxDeps/lib/box2dd.lib"
if (Test-Path $box2dLibDebug) {
    Copy-Item -Path $box2dLibDebug -Destination "$sdkStaging/lib/box2dd.lib" -Force
    Write-Host "Bundled Box2D Debug lib"
}
$box2dCmakeDir = "C:/LuxDeps/lib/cmake/box2d"
if (Test-Path $box2dCmakeDir) {
    New-Item -ItemType Directory -Path "$sdkStaging/lib/cmake/box2d" -Force | Out-Null
    Copy-Item -Path "$box2dCmakeDir/*" -Destination "$sdkStaging/lib/cmake/box2d" -Recurse -Force
    Write-Host "Bundled Box2D CMake package"
}

# Bundle SDL3 headers, import library and CMake configuration
$vcpkgBase = if ($env:VCPKG_INSTALLATION_ROOT) { $env:VCPKG_INSTALLATION_ROOT } elseif (Test-Path "C:/vcpkg") { "C:/vcpkg" } else { "" }
$sdlInclude = if ($vcpkgBase) { "$vcpkgBase/installed/x64-windows/include/SDL3" } else { "" }
if ($sdlInclude -and (Test-Path $sdlInclude)) {
    New-Item -ItemType Directory -Path "$sdkStaging/include/SDL3" -Force | Out-Null
    Copy-Item -Path "$sdlInclude/*" -Destination "$sdkStaging/include/SDL3" -Recurse -Force
    Write-Host "Bundled SDL3 headers"
}
$sdlLib = if ($vcpkgBase) { "$vcpkgBase/installed/x64-windows/lib/SDL3.lib" } else { "" }
if ($sdlLib -and (Test-Path $sdlLib)) {
    Copy-Item -Path $sdlLib -Destination "$sdkStaging/lib/SDL3.lib" -Force
    Write-Host "Bundled SDL3 import lib"
}

# Verify essential files strictly (throw on missing file)
$requiredFiles = @(
    "include/LuxEngine/Application.hpp",
    "include/box2d/box2d.h",
    "include/SDL3/SDL.h",
    "lib/LuxEngine.lib",
    "lib/LuxRuntime.lib",
    "lib/box2d.lib",
    "lib/SDL3.lib",
    "bin/SDL3.dll",
    "lib/cmake/LuxEngine/LuxEngineConfig.cmake",
    "share/LuxEngine/shaders/DIRECT3D12/default.rootsig",
    "share/LuxEngine/shaders/DIRECT3D12/lux2d.vert",
    "share/LuxEngine/shaders/DIRECT3D12/lux2d.frag",
    "licenses/LICENSE-LuxEngine.txt",
    "licenses/LICENSE-LGPLv3.txt",
    "licenses/DEPENDENCIES.md",
    "examples/MinimalGame/CMakeLists.txt",
    "examples/MinimalGame/main.cpp"
)

foreach ($f in $requiredFiles) {
    $targetPath = Join-Path $sdkStaging $f
    if (-not (Test-Path $targetPath)) {
        throw "Packaging integrity failure: required SDK file missing: $f"
    }
}

Assert-LuxWindowsRuntime -Directory (Join-Path $sdkStaging "bin")

# Create ZIP archive
Write-Host "Creating SDK archive: $sdkZip"
Compress-Archive -Path "$sdkStaging/*" -DestinationPath $sdkZip -CompressionLevel Optimal

$zipSize = (Get-Item $sdkZip).Length
$zipHash = (Get-FileHash -Path $sdkZip -Algorithm SHA256).Hash
Write-Host "=== SDK Packaging Complete ==="
Write-Host "Package: $sdkZip"
Write-Host "Size   : $([math]::Round($zipSize / 1MB, 2)) MB ($zipSize bytes)"
Write-Host "SHA256 : $zipHash"
