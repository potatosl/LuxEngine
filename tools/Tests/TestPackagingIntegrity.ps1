# Regression Test: Packaging Integrity (BUG-06 Prevention)
# Verifies that packaging scripts strictly require the requested configuration
# and reject packaging with debug CRT DLLs or silent fallbacks.

param(
    [string]$BuildDir = "build/local"
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.Parent.FullName
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
    & $psExe -ExecutionPolicy Bypass -File "$root/tools/PackageRuntime.ps1" -BuildDir $BuildDir -Configuration "NonExistentConfig_xyz"
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
    & $psExe -ExecutionPolicy Bypass -File "$root/tools/PackageEditor.ps1" -BuildDir $BuildDir -Configuration "NonExistentConfig_xyz"
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
    & $psExe -ExecutionPolicy Bypass -File "$root/tools/PackageSdk.ps1" -BuildDir "non_existent_build_dir_xyz"
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
    & $psExe -ExecutionPolicy Bypass -File "$root/tools/PackageRuntime.ps1" -BuildDir $mockDir -Configuration "Release" -OutputDir "$mockDir/dist"
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

Write-Host "`n=== Packaging Integrity Tests: ALL PASSED ==="
