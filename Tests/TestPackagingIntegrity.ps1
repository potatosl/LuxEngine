# Regression Test: Packaging Integrity (BUG-06 Prevention)
# Verifies that packaging scripts strictly require the requested configuration
# and reject packaging with debug CRT DLLs or silent fallbacks.

param(
    [string]$BuildDir = "build/local"
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

if (-not (Test-Path (Join-Path $root $BuildDir))) {
    if (Test-Path (Join-Path $root "build")) {
        $BuildDir = "build"
    }
}

Write-Host "=== Running Packaging Integrity & Strict Configuration Tests ==="

$psExe = if (Get-Command pwsh -ErrorAction SilentlyContinue) { "pwsh" } else { "powershell" }

# Test 1: Verify PackageRuntime fails when given a non-existent configuration (No silent Debug fallback)
Write-Host "1. Testing PackageRuntime rejects invalid/missing configuration..."
$threw = $false
try {
    & $psExe -ExecutionPolicy Bypass -File "$root/Tools/PackageRuntime.ps1" -BuildDir $BuildDir -Configuration "NonExistentConfig_xyz"
    if ($LASTEXITCODE -ne 0) { $threw = $true }
} catch {
    $threw = $true
}
if (-not $threw) {
    throw "Packaging integrity failure: PackageRuntime did not fail on non-existent configuration!"
}
Write-Host "   PASSED: Non-existent configuration was properly rejected."

# Test 2: Verify PackageEditor fails when given a non-existent configuration
Write-Host "2. Testing PackageEditor rejects invalid/missing configuration..."
$threw = $false
try {
    & $psExe -ExecutionPolicy Bypass -File "$root/Tools/PackageEditor.ps1" -BuildDir $BuildDir -Configuration "NonExistentConfig_xyz"
    if ($LASTEXITCODE -ne 0) { $threw = $true }
} catch {
    $threw = $true
}
if (-not $threw) {
    throw "Packaging integrity failure: PackageEditor did not fail on non-existent configuration!"
}
Write-Host "   PASSED: Non-existent configuration was properly rejected."

# Test 3: Verify PackageSdk fails when given a non-existent build directory
Write-Host "3. Testing PackageSdk rejects invalid/missing build directory..."
$threw = $false
try {
    & $psExe -ExecutionPolicy Bypass -File "$root/Tools/PackageSdk.ps1" -BuildDir "non_existent_build_dir_xyz"
    if ($LASTEXITCODE -ne 0) { $threw = $true }
} catch {
    $threw = $true
}
if (-not $threw) {
    throw "Packaging integrity failure: PackageSdk did not fail on non-existent build directory!"
}
Write-Host "   PASSED: Non-existent build directory was properly rejected."

# Test 4: Verify that Debug CRT DLLs (msvcp140d.dll, ucrtbased.dll, vcruntime140d.dll) are rejected
Write-Host "4. Testing rejection of Debug CRT DLLs in release packaging..."
$mockDir = Join-Path ([System.IO.Path]::GetTempPath()) "LuxMockPkg_$(Get-Random)"
$mockBin = Join-Path $mockDir "bin\Release"
New-Item -ItemType Directory -Path $mockBin -Force | Out-Null
Set-Content -Path (Join-Path $mockBin "LuxPlayer.exe") -Value "mock binary"
Set-Content -Path (Join-Path $mockBin "ucrtbased.dll") -Value "mock debug crt dll"

$threwDebugCrt = $false
try {
    & $psExe -ExecutionPolicy Bypass -File "$root/Tools/PackageRuntime.ps1" -BuildDir $mockDir -Configuration "Release" -OutputDir "$mockDir/dist"
    if ($LASTEXITCODE -ne 0) { $threwDebugCrt = $true }
} catch {
    $threwDebugCrt = $true
} finally {
    Remove-Item -Recurse -Force $mockDir -ErrorAction SilentlyContinue
}

if (-not $threwDebugCrt) {
    throw "Packaging integrity failure: PackageRuntime did not reject the presence of ucrtbased.dll in Release packaging!"
}
Write-Host "   PASSED: Debug CRT DLL in Release configuration was rejected."

$fixtureRoot = Join-Path $root "build/packaging-regression-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
try {
    foreach ($package in @("Runtime", "Editor", "Sdk")) {
        $prefix = if ($package -eq "Runtime") { "LuxPlayer" } else { "LuxEditor" }
        $output = Join-Path $fixtureRoot $package
        $folder = if ($package -eq "Sdk") { "LuxEngine-SDK-0.1.0-x64" } else { "$prefix-Portable-0.1.0-x64" }
        $staging = Join-Path $output $folder
        New-Item -ItemType Directory -Path $staging -Force | Out-Null
        $sentinel = Join-Path $staging "preserve.txt"
        $archive = "$staging.zip"
        Set-Content -LiteralPath $sentinel -Value "previous staging"
        Set-Content -LiteralPath $archive -Value "previous archive"
        & $psExe -ExecutionPolicy Bypass -File "$root/Tools/Package$package.ps1" -BuildDir (Join-Path $fixtureRoot "missing") -OutputDir $output
        if ($LASTEXITCODE -eq 0) { throw "$package accepted a missing build" }
        if ((Get-Content -LiteralPath $sentinel -Raw).Trim() -ne "previous staging" -or
            (Get-Content -LiteralPath $archive -Raw).Trim() -ne "previous archive") {
            throw "$package destroyed the previous package before checking the build"
        }
        & $psExe -ExecutionPolicy Bypass -File "$root/Tools/Package$package.ps1" -Version "bad/../../outside" -OutputDir $output
        if ($LASTEXITCODE -eq 0) { throw "$package accepted path traversal in Version" }
    }
    Write-Host "   PASSED: Missing builds preserve previous packages and unsafe versions are rejected."
    $portableBuild = Join-Path $fixtureRoot "portable-build"
    $portableBin = Join-Path $portableBuild "bin/Release"
    $portableShaders = Join-Path $portableBuild "shaders"
    New-Item -ItemType Directory -Path $portableBin, $portableShaders -Force | Out-Null
    foreach ($file in @("LuxPlayer.exe", "SDL3.dll", "LuxEngine.dll", "box2d.dll")) {
        Set-Content -LiteralPath (Join-Path $portableBin $file) -Value "fixture"
    }
    foreach ($file in @("Basic.vert.spv", "Basic.frag.spv")) {
        Set-Content -LiteralPath (Join-Path $portableShaders $file) -Value "shader fixture"
    }
    $portableOutput = Join-Path $fixtureRoot "portable-output"
    & $psExe -ExecutionPolicy Bypass -File "$root/Tools/PackageRuntime.ps1" -BuildDir $portableBuild -OutputDir $portableOutput
    if ($LASTEXITCODE -ne 0) { throw "Runtime packaging rejected an absolute build path with fallback shaders" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead((Join-Path $portableOutput "LuxPlayer-Portable-0.1.0-x64.zip"))
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($file in @("LuxEngine.dll", "box2d.dll", "shaders/Basic.vert.spv", "shaders/Basic.frag.spv")) {
            if ($file -notin $entries) { throw "Runtime package is missing $file" }
        }
    } finally {
        $zip.Dispose()
    }
    Write-Host "   PASSED: Absolute build paths, fallback shaders and shared runtime DLLs are packaged."
} finally {
    $resolvedFixture = [System.IO.Path]::GetFullPath($fixtureRoot)
    $fixtureParent = [System.IO.Path]::GetFullPath((Join-Path $root "build"))
    if ((Split-Path $resolvedFixture -Parent) -ne $fixtureParent) { throw "Unsafe fixture cleanup path" }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}

Write-Host "`n=== Packaging Integrity Tests: ALL PASSED ==="
exit 0
