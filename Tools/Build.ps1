param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Ninja,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$rootDir = (Get-Item $PSScriptRoot).Parent.FullName

$wingetCmake = "C:\Users\eunso\AppData\Local\Microsoft\WinGet\Links\cmake.exe"
$vsCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (Test-Path $wingetCmake) {
    $cmakeExe = $wingetCmake
} elseif (Test-Path $vsCmake) {
    $cmakeExe = $vsCmake
} else {
    $cmakeExe = "cmake.exe"
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
    & $cmakeExe -S $rootDir -B $buildDir -G "Ninja" -D CMAKE_BUILD_TYPE=$Config
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    & $cmakeExe --build $buildDir
} else {
    $buildDir = Join-Path $rootDir "build/vs2026"
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }
    & $cmakeExe -S $rootDir -B $buildDir -G "Visual Studio 18 2026" -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    & $cmakeExe --build $buildDir --config $Config
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[SUCCESS] LuxEngine built successfully!" -ForegroundColor Green
} else {
    Write-Host "`n[ERROR] Build failed." -ForegroundColor Red
}
