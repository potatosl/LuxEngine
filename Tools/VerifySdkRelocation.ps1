# Verify SDK Relocatability & Consumer Build in Debug and Release
param(
    [string]$BuildDir = "build/local",
    [string]$SdkPrefix = ""
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

Write-Host "=== Testing LuxEngine SDK Relocation & Consumer Build ==="

$cmakeExe = "cmake"
$vsCMake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (Test-Path $vsCMake) { $cmakeExe = $vsCMake }

# If no SDK prefix provided, install to a path with spaces to test relocatability
if ([string]::IsNullOrWhiteSpace($SdkPrefix)) {
    $tempBase = [System.IO.Path]::GetTempPath()
    $SdkPrefix = Join-Path $tempBase "LuxSDK Relocated Path $(Get-Random)"
}

Write-Host "SDK Relocation Target: $SdkPrefix"
if (Test-Path $SdkPrefix) {
    Remove-Item -Recurse -Force $SdkPrefix
}
New-Item -ItemType Directory -Path $SdkPrefix -Force | Out-Null

# 1. Install SDK to relocated directory (both Release and Debug)
Write-Host "1. Installing SDK to relocated path..."
& $cmakeExe --install $BuildDir --config Release --prefix $SdkPrefix
if ($LASTEXITCODE -ne 0) { throw "SDK Release install failed" }
& $cmakeExe --install $BuildDir --config Debug --prefix $SdkPrefix
if ($LASTEXITCODE -ne 0) { throw "SDK Debug install failed" }

# Copy SDL3 runtime DLL into SDK bin
$sdlDll = Join-Path $root "$BuildDir/bin/Release/SDL3.dll"
if (-not (Test-Path $sdlDll)) { $sdlDll = Join-Path $root "$BuildDir/bin/Debug/SDL3.dll" }
if (Test-Path $sdlDll) {
    Copy-Item -Path $sdlDll -Destination "$SdkPrefix/bin/SDL3.dll" -Force
}

$sdkCmake = Join-Path $SdkPrefix "lib/cmake/LuxEngine"
if (-not (Test-Path (Join-Path $sdkCmake "LuxEngineConfig.cmake"))) {
    throw "LuxEngineConfig.cmake missing in $sdkCmake"
}

# 2. Test MinimalGame Consumer against Relocated SDK in Release & Debug
$consumerTestDir = Join-Path ([System.IO.Path]::GetTempPath()) "LuxConsumerTest-$(Get-Random)"
New-Item -ItemType Directory -Path $consumerTestDir -Force | Out-Null

try {
    Copy-Item -Path "$root/examples/MinimalGame/*" -Destination $consumerTestDir -Recurse -Force

    $consumerBuild = Join-Path $consumerTestDir "build"
    Write-Host "2. Configuring MinimalGame against relocated SDK..."
    & $cmakeExe -S $consumerTestDir -B $consumerBuild -A x64 "-DLuxEngine_DIR=$sdkCmake"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for consumer" }

    # Build & Run Release
    Write-Host "3. Building & running MinimalGame (Release)..."
    & $cmakeExe --build $consumerBuild --config Release
    if ($LASTEXITCODE -ne 0) { throw "CMake build (Release) failed for consumer" }

    $releaseExe = Join-Path $consumerBuild "Release/MinimalGame.exe"
    if (-not (Test-Path $releaseExe)) {
        $releaseExe = Join-Path $consumerBuild "bin/Release/MinimalGame.exe"
    }
    if (-not (Test-Path $releaseExe)) {
        throw "MinimalGame.exe (Release) not found in $consumerBuild"
    }
    Write-Host "Running $releaseExe..."
    & $releaseExe
    if ($LASTEXITCODE -ne 0) { throw "MinimalGame.exe (Release) execution failed: exit code $LASTEXITCODE" }

    # Build & Run Debug
    Write-Host "4. Building & running MinimalGame (Debug)..."
    & $cmakeExe --build $consumerBuild --config Debug
    if ($LASTEXITCODE -ne 0) { throw "CMake build (Debug) failed for consumer" }

    $debugExe = Join-Path $consumerBuild "Debug/MinimalGame.exe"
    if (-not (Test-Path $debugExe)) {
        $debugExe = Join-Path $consumerBuild "bin/Debug/MinimalGame.exe"
    }
    if (-not (Test-Path $debugExe)) {
        throw "MinimalGame.exe (Debug) not found in $consumerBuild"
    }
    Write-Host "Running $debugExe..."
    & $debugExe
    if ($LASTEXITCODE -ne 0) { throw "MinimalGame.exe (Debug) execution failed: exit code $LASTEXITCODE" }

    Write-Host "`n=== SDK Relocation & MinimalGame Verification: ALL PASSED ==="
}
finally {
    if (Test-Path $consumerTestDir) {
        Remove-Item -Recurse -Force $consumerTestDir -ErrorAction SilentlyContinue
    }
    if (Test-Path $SdkPrefix) {
        Remove-Item -Recurse -Force $SdkPrefix -ErrorAction SilentlyContinue
    }
}
