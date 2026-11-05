<#
.SYNOPSIS
    LuxEngine Build Helper Script
.DESCRIPTION
    Configures and builds LuxEngine with Visual Studio 2026 (MSVC v145) or Ninja.
    Avoids PATH collision with MSYS2 cmake.
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Ninja,
    [switch]$Clean,
    [string]$SDL3Dir = ""
)

$ErrorActionPreference = "Stop"
$rootDir = (Get-Item $PSScriptRoot).Parent.FullName
$sdl3CMakeArg = @()
if ($SDL3Dir) {
    if (-not (Test-Path -LiteralPath (Join-Path $SDL3Dir "SDL3Config.cmake"))) {
        throw "SDL3Config.cmake not found in: $SDL3Dir"
    }
    $sdl3CMakeArg = @("-DSDL3_DIR=$SDL3Dir")
}

# Determine Windows Native CMake (prefer VS bundled or system native over MSYS2)
$vsCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (Test-Path $vsCmake) {
    $cmakeExe = $vsCmake
} else {
    $cmakeExe = (Get-Command cmake.exe | Select-Object -ExpandProperty Source)
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  LuxEngine Build System" -ForegroundColor Cyan
Write-Host "  Using CMake: $cmakeExe" -ForegroundColor Gray
Write-Host "  Configuration: $Config" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan

if ($Ninja) {
    $buildDir = Join-Path $rootDir "build/ninja-$($Config.ToLower())"
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }
    & $cmakeExe -S $rootDir -B $buildDir -G "Ninja" -D CMAKE_BUILD_TYPE=$Config @sdl3CMakeArg
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    & $cmakeExe --build $buildDir
} else {
    $buildDir = Join-Path $rootDir "build/vs2026"
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }
    & $cmakeExe -S $rootDir -B $buildDir -G "Visual Studio 18 2026" -A x64 @sdl3CMakeArg
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    & $cmakeExe --build $buildDir --config $Config
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[SUCCESS] LuxEngine built successfully!" -ForegroundColor Green
} else {
    Write-Host "`n[ERROR] Build failed." -ForegroundColor Red
}
