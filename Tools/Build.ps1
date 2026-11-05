param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Ninja,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$rootDir = (Get-Item $PSScriptRoot).Parent.FullName

$portableCmake = Join-Path $rootDir "Tools/cmake/bin/cmake.exe"
$cmakeExe = if (Test-Path -LiteralPath $portableCmake) { $portableCmake } else { (Get-Command cmake -ErrorAction Stop).Source }

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
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) { throw "Run Ninja builds from a compiler developer shell." }
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
    throw "Build failed."
}
