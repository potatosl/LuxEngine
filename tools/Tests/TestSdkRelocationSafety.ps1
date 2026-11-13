# Regression Test: Ensure VerifySdkRelocation.ps1 never deletes user data
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

Write-Host "=== Running Regression Test: SdkRelocation User Data Safety ==="

$tempBase = [System.IO.Path]::GetTempPath()
$testUserDataDir = Join-Path $tempBase "LuxUserTestDir_Preserve_$(Get-Random)"
New-Item -ItemType Directory -Path $testUserDataDir -Force | Out-Null

$dummyUserFile = Join-Path $testUserDataDir "important_user_work.txt"
$dummyContent = "CRITICAL USER DATA - DO NOT DELETE $(Get-Date)"
Set-Content -Path $dummyUserFile -Value $dummyContent -Encoding ascii

$subfolder = Join-Path $testUserDataDir "subproject"
New-Item -ItemType Directory -Path $subfolder -Force | Out-Null
$dummySubFile = Join-Path $subfolder "code.cpp"
Set-Content -Path $dummySubFile -Value "// User Code" -Encoding ascii

$threwExpected = $false
try {
    Write-Host "Invoking VerifySdkRelocation with existing user directory: $testUserDataDir"
    $psExe = if (Get-Command pwsh -ErrorAction SilentlyContinue) { "pwsh" } else { "powershell" }
    & $psExe -ExecutionPolicy Bypass -File "$root/tools/VerifySdkRelocation.ps1" -BuildDir $BuildDir -SdkPrefix $testUserDataDir
    if ($LASTEXITCODE -ne 0) {
        $threwExpected = $true
    }
}
catch {
    Write-Host "Caught expected exception: $_"
    $threwExpected = $true
}

# 1. Verify that invocation was rejected
if (-not $threwExpected) {
    throw "Safety test failed: VerifySdkRelocation did not reject existing non-empty directory!"
}

# 2. Verify all user files and subfolders are 100% intact!
if (-not (Test-Path $dummyUserFile)) {
    throw "CRITICAL FAILURE: User file '$dummyUserFile' was deleted!"
}
$readContent = Get-Content -Path $dummyUserFile -Raw
if ($readContent.Trim() -ne $dummyContent.Trim()) {
    throw "CRITICAL FAILURE: User file content was modified!"
}
if (-not (Test-Path $dummySubFile)) {
    throw "CRITICAL FAILURE: User subfolder file was deleted!"
}

Write-Host "User files verified intact! Preserved perfectly."

# Clean up our test temp directory
Remove-Item -Recurse -Force $testUserDataDir -ErrorAction SilentlyContinue

Write-Host "=== SdkRelocation User Data Safety Test: PASSED ==="
