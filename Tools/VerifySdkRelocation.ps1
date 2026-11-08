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

$isUserSpecifiedPrefix = -not [string]::IsNullOrWhiteSpace($SdkPrefix)
$shouldCleanupSdk = -not $isUserSpecifiedPrefix

if (-not $isUserSpecifiedPrefix) {
    # If no SDK prefix provided, test relocatability using a path with spaces and Korean Unicode
    $tempBase = [System.IO.Path]::GetTempPath()
    $koreanPart = [string]::new([char[]]@(0xD55C, 0xAE00, 0xACBD, 0xB85C)) # "한글경로"
    $SdkPrefix = Join-Path $tempBase "LuxSDK Relocated $koreanPart $(Get-Random)"
    New-Item -ItemType Directory -Path $SdkPrefix -Force | Out-Null
} else {
    # User specified an SdkPrefix: NEVER delete or overwrite existing user directories!
    if (Test-Path $SdkPrefix) {
        $existingItems = Get-ChildItem -Path $SdkPrefix -Force -ErrorAction SilentlyContinue
        if ($existingItems -and $existingItems.Count -gt 0) {
            throw "Target SdkPrefix directory already exists and is not empty: '$SdkPrefix'. VerifySdkRelocation refuses to overwrite or delete existing user directories to prevent data loss. Please specify an empty directory or omit -SdkPrefix to use a dedicated isolated path."
        }
    } else {
        New-Item -ItemType Directory -Path $SdkPrefix -Force | Out-Null
    }
}

Write-Host "SDK Relocation Target: $SdkPrefix"

$consumerTestDir = Join-Path ([System.IO.Path]::GetTempPath()) "LuxConsumerTest-$(Get-Random)"
New-Item -ItemType Directory -Path $consumerTestDir -Force | Out-Null

try {
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

    # Bundle Box2D headers, libraries and CMake files into relocated SDK
    $box2dInclude = "C:/LuxDeps/include/box2d"
    if (Test-Path $box2dInclude) {
        New-Item -ItemType Directory -Path "$SdkPrefix/include/box2d" -Force | Out-Null
        Copy-Item -Path "$box2dInclude/*" -Destination "$SdkPrefix/include/box2d" -Recurse -Force
    }
    $box2dLibRelease = "C:/LuxDeps/lib/box2d.lib"
    if (Test-Path $box2dLibRelease) {
        Copy-Item -Path $box2dLibRelease -Destination "$SdkPrefix/lib/box2d.lib" -Force
    }
    $box2dLibDebug = "C:/LuxDeps/lib/box2dd.lib"
    if (Test-Path $box2dLibDebug) {
        Copy-Item -Path $box2dLibDebug -Destination "$SdkPrefix/lib/box2dd.lib" -Force
    }
    $box2dCmakeDir = "C:/LuxDeps/lib/cmake/box2d"
    if (Test-Path $box2dCmakeDir) {
        New-Item -ItemType Directory -Path "$SdkPrefix/lib/cmake/box2d" -Force | Out-Null
        Copy-Item -Path "$box2dCmakeDir/*" -Destination "$SdkPrefix/lib/cmake/box2d" -Recurse -Force
    }

    # Bundle SDL3 headers, import library and CMake files into relocated SDK
    $vcpkgBase = if ($env:VCPKG_INSTALLATION_ROOT) { $env:VCPKG_INSTALLATION_ROOT } elseif (Test-Path "C:/vcpkg") { "C:/vcpkg" } else { "" }
    $sdlInclude = if ($vcpkgBase) { "$vcpkgBase/installed/x64-windows/include/SDL3" } else { "" }
    if ($sdlInclude -and (Test-Path $sdlInclude)) {
        New-Item -ItemType Directory -Path "$SdkPrefix/include/SDL3" -Force | Out-Null
        Copy-Item -Path "$sdlInclude/*" -Destination "$SdkPrefix/include/SDL3" -Recurse -Force
    }
    $sdlLib = if ($vcpkgBase) { "$vcpkgBase/installed/x64-windows/lib/SDL3.lib" } else { "" }
    if ($sdlLib -and (Test-Path $sdlLib)) {
        Copy-Item -Path $sdlLib -Destination "$SdkPrefix/lib/SDL3.lib" -Force
    }
    $sdlCmakeDir = if ($vcpkgBase) { "$vcpkgBase/installed/x64-windows/share/sdl3" } else { "" }
    if ($sdlCmakeDir -and (Test-Path $sdlCmakeDir)) {
        New-Item -ItemType Directory -Path "$SdkPrefix/share/sdl3" -Force | Out-Null
        Copy-Item -Path "$sdlCmakeDir/*" -Destination "$SdkPrefix/share/sdl3" -Recurse -Force
    }

    $sdkCmake = Join-Path $SdkPrefix "lib/cmake/LuxEngine"
    if (-not (Test-Path (Join-Path $sdkCmake "LuxEngineConfig.cmake"))) {
        throw "LuxEngineConfig.cmake missing in $sdkCmake"
    }

    # 2. Test MinimalGame Consumer against Relocated SDK in Release & Debug
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
    # NEVER delete user-specified directories! Only clean up if auto-generated by the script.
    if ($shouldCleanupSdk -and (Test-Path $SdkPrefix)) {
        Remove-Item -Recurse -Force $SdkPrefix -ErrorAction SilentlyContinue
    }
}
