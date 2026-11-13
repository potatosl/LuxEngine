# Package LuxPlayer Game Runtime into a standalone portable ZIP distribution
param(
    [string]$BuildDir = "build/local",
    [string]$OutputDir = "dist",
    [string]$Version = "0.1.0",
    [string]$Configuration = "Release"
)

. (Join-Path $PSScriptRoot "Utf8.ps1")
. (Join-Path $PSScriptRoot "WindowsRuntime.ps1")
$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

if ($Configuration -ne "Release" -and $Configuration -ne "Debug") {
    throw "Packaging failure: Unsupported configuration '$Configuration'. Must be 'Release' or 'Debug'."
}

$suffix = if ($Configuration -eq "Release") { "" } else { "-Debug" }
$outDirResolved = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $root $OutputDir }
$playerStaging = Join-Path $outDirResolved "LuxPlayer-Portable-$Version$suffix-x64"
$playerZip = Join-Path $outDirResolved "LuxPlayer-Portable-$Version$suffix-x64.zip"

Write-Host "=== Packaging LuxPlayer Runtime (v$Version, Configuration=$Configuration) ==="

if (Test-Path $playerStaging) {
    Remove-Item -Recurse -Force $playerStaging
}
if (Test-Path $playerZip) {
    Remove-Item -Force $playerZip
}
New-Item -ItemType Directory -Path $playerStaging -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $playerZip) -Force | Out-Null

$buildDirResolved = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$binSource = Join-Path $buildDirResolved "bin/$Configuration"
$playerExe = Join-Path $binSource "LuxPlayer.exe"
if (-not (Test-Path $playerExe)) {
    throw "Packaging failure: $Configuration executable LuxPlayer.exe not found in $binSource. Build $Configuration first. Silent fallback to other configurations is forbidden for public release packaging."
}

# Verify no debug CRT runtime DLLs in Release package
if ($Configuration -eq "Release") {
    $debugDlls = Get-ChildItem -Path $binSource -Filter "*d.dll" -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -match "^(vcruntime.*d|msvcp.*d|ucrtbase.*d|ucrtbased|Qt6.*d|SDL3d)\.dll$"
    }
    if ($debugDlls) {
        throw "Packaging failure: Debug runtime DLLs detected in Release binary directory: $($debugDlls.Name -join ', ')"
    }
}

# Copy executable
Copy-Item -Path $playerExe -Destination $playerStaging -Force

# Copy the Forge renderer, native shaders, GPU configuration and C# host.
Copy-LuxWindowsRuntime -Source $binSource -Destination $playerStaging

# Bundle the standalone player's dedicated runtime-verification scene.
$scenesDir = Join-Path $playerStaging "scenes"
New-Item -ItemType Directory -Path $scenesDir -Force | Out-Null
$playerSceneSource = Join-Path $root "samples/ScenePlayer/Scene.lux"
if (-not (Test-Path $playerSceneSource -PathType Leaf)) {
    throw "Packaging failure: Runtime verification scene not found: $playerSceneSource"
}
Copy-Item -Path $playerSceneSource -Destination (Join-Path $scenesDir "ScenePlayer.lux") -Force

# Copy licenses
$licensesDir = Join-Path $playerStaging "licenses"
New-Item -ItemType Directory -Path $licensesDir -Force | Out-Null
Copy-Item -Path "$root/docs/licenses/*" -Destination $licensesDir -Recurse -Force

# Write Standalone Distribution README
$playerReadme = @"
# LuxPlayer - Standalone Game Runtime (v$Version)

LuxEngine standalone player for running packaged 2D/3D games without editor or development tools.

## System Requirements
- Windows 10 64-bit or Windows 11 64-bit
- Direct3D 12 compatible GPU and display driver

## Usage
- Run runtime verification scene:
  LuxPlayer.exe scenes/ScenePlayer.lux
- Run custom scene:
  LuxPlayer.exe <path/to/scene.lux> [--title "Game Title"] [--size 1280 720] [--fullscreen] [--frames N]

## Third-Party Licenses
See the `licenses/` directory for full license texts and dependency details.
- SDL3 (zlib)
- Box2D (zlib)
- The Forge (Apache-2.0)
- .NET 10 runtime is required to execute C# game scripts
"@
$playerReadme | Out-File -FilePath (Join-Path $playerStaging "README.md") -Encoding utf8

# Verify essential files strictly (throw immediately on missing file)
$requiredPlayerFiles = @(
    "LuxPlayer.exe",
    "SDL3.dll",
    "scenes/ScenePlayer.lux",
    "licenses/LICENSE-LuxEngine.txt",
    "licenses/DEPENDENCIES.md"
)

foreach ($f in $requiredPlayerFiles) {
    $targetPath = Join-Path $playerStaging $f
    if (-not (Test-Path $targetPath)) {
        throw "Packaging integrity failure: required Runtime file missing: $f"
    }
}

Assert-LuxWindowsRuntime -Directory $playerStaging

# Verify no debug runtime DLLs are packaged in Release build
if ($binSource -like "*Release*") {
    $debugDlls = Get-ChildItem -Path $playerStaging -Filter "*d.dll" -Recurse | Where-Object {
        $_.Name -match "^(ucrtbase|vcruntime140|msvcp140|SDL3)d\.dll$"
    }
    if ($debugDlls -and $debugDlls.Count -gt 0) {
        throw "Packaging integrity failure: Debug DLLs found in Release Player package: $($debugDlls.Name -join ', ')"
    }
}

# Create ZIP archive
Write-Host "Creating Runtime archive: $playerZip"
Compress-Archive -Path "$playerStaging/*" -DestinationPath $playerZip -CompressionLevel Optimal

$zipSize = (Get-Item $playerZip).Length
$zipHash = (Get-FileHash -Path $playerZip -Algorithm SHA256).Hash
Write-Host "=== Runtime Packaging Complete ==="
Write-Host "Package: $playerZip"
Write-Host "Size   : $([math]::Round($zipSize / 1MB, 2)) MB ($zipSize bytes)"
Write-Host "SHA256 : $zipHash"
