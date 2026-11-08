# Package LuxPlayer Game Runtime into a standalone portable ZIP distribution
param(
    [string]$BuildDir = "build/local",
    [string]$OutputDir = "dist",
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

Write-Host "=== Packaging LuxPlayer Runtime (v$Version) ==="
$playerStaging = Join-Path $root "$OutputDir/LuxPlayer-Portable-$Version-x64"
$playerZip = Join-Path $root "$OutputDir/LuxPlayer-Portable-$Version-x64.zip"

if (Test-Path $playerStaging) {
    Remove-Item -Recurse -Force $playerStaging
}
if (Test-Path $playerZip) {
    Remove-Item -Force $playerZip
}
New-Item -ItemType Directory -Path $playerStaging -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $playerZip) -Force | Out-Null

$binSource = Join-Path $root "$BuildDir/bin/Release"
if (-not (Test-Path $binSource)) {
    $binSource = Join-Path $root "$BuildDir/bin/Debug"
}
if (-not (Test-Path (Join-Path $binSource "LuxPlayer.exe"))) {
    throw "LuxPlayer.exe not found in $binSource. Build LuxPlayer first."
}

# Copy executable
Copy-Item -Path (Join-Path $binSource "LuxPlayer.exe") -Destination $playerStaging -Force

# Copy SDL3 runtime DLL
if (Test-Path (Join-Path $binSource "SDL3.dll")) {
    Copy-Item -Path (Join-Path $binSource "SDL3.dll") -Destination $playerStaging -Force
}

# Copy shaders
$shadersDir = Join-Path $playerStaging "shaders"
New-Item -ItemType Directory -Path $shadersDir -Force | Out-Null
$shaderSource = Join-Path $binSource "shaders"
if (-not (Test-Path $shaderSource)) {
    $shaderSource = Join-Path $root "$BuildDir/shaders"
}
if (Test-Path $shaderSource) {
    Copy-Item -Path "$shaderSource/*" -Destination $shadersDir -Recurse -Force
}

# Copy licenses
$licensesDir = Join-Path $playerStaging "licenses"
New-Item -ItemType Directory -Path $licensesDir -Force | Out-Null
Copy-Item -Path "$root/licenses/*" -Destination $licensesDir -Recurse -Force

# Write Standalone Distribution README
$playerReadme = @"
# LuxPlayer - Standalone Game Runtime

LuxEngine standalone player for running packaged 2D/3D games without editor or development tools.

## System Requirements
- Windows 10 64-bit or Windows 11 64-bit
- Vulkan 1.1+ compatible GPU and display driver

## Usage
LuxPlayer.exe <path/to/scene.lux> [--title "Game Title"] [--size 1280 720] [--fullscreen]
"@
$playerReadme | Out-File -FilePath (Join-Path $playerStaging "README.md") -Encoding utf8

# Create ZIP archive
Write-Host "Creating Runtime archive: $playerZip"
Compress-Archive -Path "$playerStaging/*" -DestinationPath $playerZip -CompressionLevel Optimal

$zipSize = (Get-Item $playerZip).Length
$zipHash = (Get-FileHash -Path $playerZip -Algorithm SHA256).Hash
Write-Host "=== Runtime Packaging Complete ==="
Write-Host "Package: $playerZip"
Write-Host "Size   : $([math]::Round($zipSize / 1MB, 2)) MB ($zipSize bytes)"
Write-Host "SHA256 : $zipHash"
