# Package LuxEngine SDK into a portable ZIP distribution
param(
    [string]$BuildDir = "build/local",
    [string]$OutputDir = "dist",
    [ValidatePattern("^[A-Za-z0-9][A-Za-z0-9._-]*$")]
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

$buildDirResolved = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }))
$cachePath = Join-Path $buildDirResolved "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw "CMake cache not found in build directory: $buildDirResolved"
}
$buildCache = @{}
foreach ($line in Get-Content -LiteralPath $cachePath) {
    if ($line -match '^([^#/:][^:]*):[^=]*=(.*)$') {
        $buildCache[$matches[1]] = $matches[2]
    }
}
$cmakeExe = $buildCache["CMAKE_COMMAND"]
if (-not $cmakeExe -or -not (Test-Path -LiteralPath $cmakeExe -PathType Leaf)) {
    $cmakeExe = (Get-Command cmake -ErrorAction Stop).Source
}
foreach ($sourceFile in @("lib/Release/LuxEngine.lib", "lib/Release/LuxRuntime.lib", "lib/Debug/LuxEngined.lib", "lib/Debug/LuxRuntimed.lib")) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildDirResolved $sourceFile) -PathType Leaf)) {
        throw "Required SDK build output missing: $sourceFile. Build Release and Debug first."
    }
}
foreach ($dependency in @(@("SDL3_DIR", "SDL3Config.cmake"), @("box2d_DIR", "box2dConfig.cmake"))) {
    $dependencyDir = $buildCache[$dependency[0]]
    if (-not $dependencyDir -or -not (Test-Path -LiteralPath (Join-Path $dependencyDir $dependency[1]) -PathType Leaf)) {
        throw "Dependency configuration missing from build cache: $($dependency[0])"
    }
}

function Bundle-Dependency([string]$cacheKey, [string]$headerFolder, [string]$libraryPrefix) {
    $configDir = $buildCache[$cacheKey]
    if (-not $configDir -or -not (Test-Path -LiteralPath $configDir -PathType Container)) {
        throw "Dependency configuration missing from build cache: $cacheKey"
    }
    $configDir = (Resolve-Path -LiteralPath $configDir).Path
    $prefix = $configDir
    while (-not (Test-Path -LiteralPath (Join-Path $prefix "include/$headerFolder") -PathType Container)) {
        $parent = Split-Path -Parent $prefix
        if (-not $parent -or $parent -eq $prefix) {
            throw "Dependency headers not found above $configDir"
        }
        $prefix = $parent
    }
    $includeDestination = Join-Path $sdkStaging "include/$headerFolder"
    New-Item -ItemType Directory -Path $includeDestination -Force | Out-Null
    Copy-Item -Path (Join-Path $prefix "include/$headerFolder/*") -Destination $includeDestination -Recurse -Force
    $configRelative = $configDir.Substring($prefix.TrimEnd('\', '/').Length).TrimStart('\', '/')
    $configDestination = Join-Path $sdkStaging $configRelative
    New-Item -ItemType Directory -Path $configDestination -Force | Out-Null
    Copy-Item -Path (Join-Path $configDir "*") -Destination $configDestination -Recurse -Force
    foreach ($directory in @("lib", "debug/lib", "bin", "debug/bin")) {
        $source = Join-Path $prefix $directory
        if (-not (Test-Path -LiteralPath $source -PathType Container)) { continue }
        $extension = if ($directory.EndsWith("lib")) { ".lib" } else { ".dll" }
        $files = @(Get-ChildItem -LiteralPath $source -File -Filter "$libraryPrefix*$extension")
        if ($files.Count -eq 0) { continue }
        $destination = Join-Path $sdkStaging $directory
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        $files | Copy-Item -Destination $destination -Force
    }
    Write-Host "Bundled $headerFolder from $prefix"
}

Write-Host "=== Packaging LuxEngine SDK (v$Version) ==="
$outDirResolved = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $root $OutputDir }
$outDirResolved = [System.IO.Path]::GetFullPath($outDirResolved).TrimEnd('\', '/')
$sdkStaging = Join-Path $outDirResolved "LuxEngine-SDK-$Version-x64"
$sdkZip = Join-Path $outDirResolved "LuxEngine-SDK-$Version-x64.zip"
foreach ($target in @($sdkStaging, $sdkZip)) {
    if (-not [System.IO.Path]::GetFullPath($target).StartsWith($outDirResolved + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Package output must stay inside $outDirResolved"
    }
}

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
& $cmakeExe --install $buildDirResolved --config Release --prefix $sdkStaging
if ($LASTEXITCODE -ne 0) { throw "cmake --install Release failed" }

Write-Host "Installing Debug SDK to staging..."
& $cmakeExe --install $buildDirResolved --config Debug --prefix $sdkStaging
if ($LASTEXITCODE -ne 0) { throw "cmake --install Debug failed" }

Bundle-Dependency "SDL3_DIR" "SDL3" "SDL3"
Bundle-Dependency "box2d_DIR" "box2d" "box2d"
# Verify essential files strictly (throw on missing file)
$requiredFiles = @(
    "include/Engine/Application.hpp",
    "include/box2d/box2d.h",
    "include/SDL3/SDL.h",
    "lib/LuxEngine.lib",
    "lib/LuxRuntime.lib",
    "lib/LuxEngined.lib",
    "lib/LuxRuntimed.lib",
    "lib/box2d.lib",
    "lib/box2dd.lib",
    "lib/SDL3.lib",
    "bin/SDL3.dll",
    "lib/cmake/LuxEngine/LuxEngineConfig.cmake",
    "lib/cmake/box2d/box2dConfig.cmake",
    "share/LuxEngine/shaders/Basic.vert.spv",
    "share/LuxEngine/shaders/Basic.frag.spv",
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

# Create ZIP archive
Write-Host "Creating SDK archive: $sdkZip"
Compress-Archive -Path "$sdkStaging/*" -DestinationPath $sdkZip -CompressionLevel Optimal

$zipSize = (Get-Item $sdkZip).Length
$zipHash = (Get-FileHash -Path $sdkZip -Algorithm SHA256).Hash
Write-Host "=== SDK Packaging Complete ==="
Write-Host "Package: $sdkZip"
Write-Host "Size   : $([math]::Round($zipSize / 1MB, 2)) MB ($zipSize bytes)"
Write-Host "SHA256 : $zipHash"
