. (Join-Path $PSScriptRoot "Utf8.ps1")
<#
.SYNOPSIS
    Opens LuxEngine in Visual Studio 2026
#>
$rootDir = (Get-Item $PSScriptRoot).Parent.FullName
$devenv = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe"

if (-not (Test-Path $devenv)) {
    throw "Visual Studio 2026 (devenv.exe) not found at: $devenv"
}

$solution = Join-Path $rootDir "build\vs2026\LuxEngine.slnx"
if (Test-Path $solution) {
    Write-Host "Opening LuxEngine solution: $solution" -ForegroundColor Cyan
    Start-Process -FilePath $devenv -ArgumentList "`"$solution`""
} else {
    Write-Host "Opening LuxEngine root folder in Visual Studio 2026: $rootDir" -ForegroundColor Cyan
    Start-Process -FilePath $devenv -ArgumentList "`"$rootDir`""
}
