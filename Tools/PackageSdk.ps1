# Package LuxEngine SDK into a portable ZIP distribution
param(
    [string]$BuildDir = "build/local",
    [string]$OutputDir = "dist",
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

Write-Host "=== Packaging LuxEngine SDK (v$Version) ==="
$sdkStaging = Join-Path $root "$OutputDir/LuxEngine-SDK-$Version-x64"
$sdkZip = Join-Path $root "$OutputDir/LuxEngine-SDK-$Version-x64.zip"

if (Test-Path $sdkStaging) {
    Remove-Item -Recurse -Force $sdkStaging
}
if (Test-Path $sdkZip) {
    Remove-Item -Force $sdkZip
}
New-Item -ItemType Directory -Path $sdkStaging -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $sdkZip) -Force | Out-Null

# Install Release and Debug configurations into the staging directory
Write-Host "Installing Release SDK to staging..."
& cmake --install $BuildDir --config Release --prefix $sdkStaging
if ($LASTEXITCODE -ne 0) { throw "cmake --install Release failed" }

Write-Host "Installing Debug SDK to staging..."
& cmake --install $BuildDir --config Debug --prefix $sdkStaging
if ($LASTEXITCODE -ne 0) { throw "cmake --install Debug failed" }

# Copy SDL3 runtime DLL into bin if available in build directory
$sdlDll = Join-Path $root "$BuildDir/bin/Release/SDL3.dll"
if (-not (Test-Path $sdlDll)) {
    $sdlDll = Join-Path $root "$BuildDir/bin/Debug/SDL3.dll"
}
if (Test-Path $sdlDll) {
    Copy-Item -Path $sdlDll -Destination "$sdkStaging/bin/SDL3.dll" -Force
    Write-Host "Copied SDL3.dll to SDK bin/"
}

# Verify essential files
$requiredFiles = @(
    "include/Engine/Engine/LuxEngine.hpp",
    "lib/LuxEngine.lib",
    "lib/LuxRuntime.lib",
    "lib/cmake/LuxEngine/LuxEngineConfig.cmake",
    "share/LuxEngine/shaders/Basic.vert.spv",
    "share/LuxEngine/shaders/Basic.frag.spv",
    "licenses/LICENSE-LuxEngine.txt",
    "examples/MinimalGame/CMakeLists.txt",
    "examples/MinimalGame/main.cpp"
)

foreach ($f in $requiredFiles) {
    $targetPath = Join-Path $sdkStaging $f
    # Check alternate include prefix if nested
    if (-not (Test-Path $targetPath)) {
        $altPath = Join-Path $sdkStaging ($f -replace "Engine/Engine", "Engine")
        if (-not (Test-Path $altPath)) {
            Write-Warning "Missing expected SDK file: $f"
        }
    }
}

# Create ZIP archive
Write-Host "Creating SDK archive: $sdkZip"
Compress-Archive -Path "$sdkStaging/*" -DestinationPath $sdkZip -CompressionLevel Optimal

$zipSize = (Get-Item $sdkZip).Length
$zipHash = (Get-FileHash -Path $sdkZip -Algorithm SHA256).Hash
Write-Host "=== SDK Packaging Complete ==="
Write-Host "Package: $sdkZip"
Write-Host "Size   : $([math]::Round($zipSize / 1MB, 2)) MB ($zipSize bytes)"
Write-Host "SHA256 : $zipHash"
