# Package LuxEditor into a standalone portable ZIP distribution
param(
    [string]$BuildDir = "build/local",
    [string]$OutputDir = "dist",
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

Write-Host "=== Packaging LuxEditor Standalone Portable (v$Version) ==="
$editorStaging = Join-Path $root "$OutputDir/LuxEditor-Portable-$Version-x64"
$editorZip = Join-Path $root "$OutputDir/LuxEditor-Portable-$Version-x64.zip"

if (Test-Path $editorStaging) {
    Remove-Item -Recurse -Force $editorStaging
}
if (Test-Path $editorZip) {
    Remove-Item -Force $editorZip
}
New-Item -ItemType Directory -Path $editorStaging -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $editorZip) -Force | Out-Null

$binSource = Join-Path $root "$BuildDir/bin/Release"
if (-not (Test-Path $binSource)) {
    $binSource = Join-Path $root "$BuildDir/bin/Debug"
}
if (-not (Test-Path (Join-Path $binSource "LuxEditor.exe"))) {
    throw "LuxEditor.exe not found in $binSource. Build LuxEditor first."
}

# Copy executables
Copy-Item -Path (Join-Path $binSource "LuxEditor.exe") -Destination $editorStaging -Force
if (Test-Path (Join-Path $binSource "LuxPlayer.exe")) {
    Copy-Item -Path (Join-Path $binSource "LuxPlayer.exe") -Destination $editorStaging -Force
}

# Copy all runtime DLLs
Get-ChildItem -Path $binSource -Filter "*.dll" | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $editorStaging -Force
}

# Search locations for Qt runtime and tools (vcpkg, standalone Qt, CI environment)
$qtRoots = @()
if ($env:Qt6_DIR) {
    $qtRoots += (Split-Path (Split-Path (Split-Path $env:Qt6_DIR)))
}
$qtRoots += "C:\vcpkg\installed\x64-windows"
$qtRoots += "C:\vcpkg\installed\x64-windows\Qt6"
$qtRoots += "C:\Qt"

# Copy Qt platform plugin
$platformsDir = Join-Path $editorStaging "platforms"
New-Item -ItemType Directory -Path $platformsDir -Force | Out-Null
if (Test-Path (Join-Path $binSource "platforms")) {
    Get-ChildItem -Path (Join-Path $binSource "platforms") -Filter "*.dll" | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination $platformsDir -Force
    }
}
if (-not (Test-Path (Join-Path $platformsDir "qoffscreen.dll"))) {
    foreach ($rootCand in $qtRoots) {
        if (Test-Path $rootCand) {
            $candOffscreen = Get-ChildItem -Path $rootCand -Filter "qoffscreen.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($candOffscreen) {
                Copy-Item -Path $candOffscreen.FullName -Destination $platformsDir -Force
                break
            }
        }
    }
}

# Copy shaders
$shadersDir = Join-Path $editorStaging "shaders"
New-Item -ItemType Directory -Path $shadersDir -Force | Out-Null
$shaderSource = Join-Path $binSource "shaders"
if (-not (Test-Path $shaderSource)) {
    $shaderSource = Join-Path $root "$BuildDir/shaders"
}
if (Test-Path $shaderSource) {
    Copy-Item -Path "$shaderSource/*" -Destination $shadersDir -Recurse -Force
}

# Copy licenses
$licensesDir = Join-Path $editorStaging "licenses"
New-Item -ItemType Directory -Path $licensesDir -Force | Out-Null
Copy-Item -Path "$root/licenses/*" -Destination $licensesDir -Recurse -Force

# Write Standalone Distribution README
$editorReadme = @"
# LuxEditor — Standalone Portable Edition (v$Version)

LuxEngine 2D/3D Game Editor portable package for Windows 10/11 x64.
No external installation of Qt, CMake, or Visual Studio is required to run the editor.

## System Requirements
- Windows 10 64-bit or Windows 11 64-bit
- Vulkan 1.1+ compatible GPU and up-to-date display driver (NVIDIA GeForce, AMD Radeon, or Intel Arc/UHD)
- 4 GB RAM minimum (8 GB recommended)

## How to Run
Double-click `LuxEditor.exe` to launch the editor.

## Command Line Usage
- `LuxEditor.exe` : Launch graphical editor workspace
- `LuxEditor.exe --version` : Print editor and engine version
- `LuxEditor.exe --smoke-test` : Run headless initialization verification

## Third-Party Licenses
See the `licenses/` directory for full license texts and dependency details.
- Qt 6 (LGPLv3)
- SDL3 (zlib)
- Box2D (zlib)
- libzip (3-clause BSD)
- Vulkan Memory Allocator (MIT)
"@
$editorReadme | Out-File -FilePath (Join-Path $editorStaging "README.md") -Encoding utf8

# Check for windeployqt to deploy all required Qt runtime dependencies
$windeployqt = $null
foreach ($rootCand in $qtRoots) {
    if (Test-Path $rootCand) {
        $candWindeployqt = Get-ChildItem -Path $rootCand -Filter "windeployqt.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($candWindeployqt) {
            $windeployqt = $candWindeployqt.FullName
            break
        }
    }
}
if (-not $windeployqt) {
    $cmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($cmd) { $windeployqt = $cmd.Source }
}
if ($windeployqt -and (Test-Path $windeployqt)) {
    Write-Host "Running windeployqt ($windeployqt) to ensure all required Qt dependencies are bundled..."
    & $windeployqt --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw (Join-Path $editorStaging "LuxEditor.exe")
}

# Verify essential files strictly (throw immediately on missing file)
$requiredEditorFiles = @(
    "LuxEditor.exe",
    "SDL3.dll",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "platforms/qwindows.dll",
    "shaders/Basic.vert.spv",
    "shaders/Basic.frag.spv",
    "licenses/LICENSE-LuxEngine.txt",
    "licenses/LICENSE-Qt6.txt",
    "licenses/LICENSE-LGPLv3.txt",
    "licenses/DEPENDENCIES.md"
)

foreach ($f in $requiredEditorFiles) {
    $targetPath = Join-Path $editorStaging $f
    if (-not (Test-Path $targetPath)) {
        throw "Packaging integrity failure: required Editor file missing: $f"
    }
}

# Verify no debug runtime DLLs are accidentally packaged in Release build
if ($binSource -like "*Release*") {
    $debugDlls = Get-ChildItem -Path $editorStaging -Filter "*d.dll" -Recurse | Where-Object {
        $_.Name -match "^(ucrtbase|vcruntime140|msvcp140|Qt6Core|Qt6Gui|Qt6Widgets|SDL3)d\.dll$"
    }
    if ($debugDlls -and $debugDlls.Count -gt 0) {
        throw "Packaging integrity failure: Debug DLLs found in Release Editor package: $($debugDlls.Name -join ', ')"
    }
}

# Create ZIP archive
Write-Host "Creating Editor archive: $editorZip"
Compress-Archive -Path "$editorStaging/*" -DestinationPath $editorZip -CompressionLevel Optimal

$zipSize = (Get-Item $editorZip).Length
$zipHash = (Get-FileHash -Path $editorZip -Algorithm SHA256).Hash
Write-Host "=== Editor Packaging Complete ==="
Write-Host "Package: $editorZip"
Write-Host "Size   : $([math]::Round($zipSize / 1MB, 2)) MB ($zipSize bytes)"
Write-Host "SHA256 : $zipHash"
