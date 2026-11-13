# Verify portable runtime contents without requiring a compiler, Qt install or GPU.
$ErrorActionPreference = "Stop"
$repository = (Get-Item $PSScriptRoot).Parent.Parent.FullName
. (Join-Path $repository "tools/WindowsRuntime.ps1")

function Check-File {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Expected packaged file is missing: $Path"
    }
}

function Expect-RuntimeFailure {
    param([string]$Directory, [string]$Missing)
    $failed = $false
    try { Assert-LuxWindowsRuntime -Directory $Directory }
    catch { $failed = $_.Exception.Message.Contains($Missing) }
    if (-not $failed) { throw "A missing or empty runtime file was accepted: $Missing" }
}

$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "Lux packaging 한글 $([guid]::NewGuid())"
try {
    $source = Join-Path $testDirectory "build/bin/Release"
    foreach ($relative in (Get-LuxWindowsRuntimeFiles)) {
        $path = Join-Path $source $relative
        New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null
        [System.IO.File]::WriteAllText($path, "fixture: $relative")
    }
    [System.IO.File]::WriteAllText((Join-Path $source "dxil.dll"), "optional dependency")
    [System.IO.File]::WriteAllText((Join-Path $source "managed/GameScripts.dll"), "game script")
    [System.IO.File]::WriteAllText((Join-Path $source "managed/LuxEngine.ScriptCore.runtimeconfig.json"), "{}")
    Assert-LuxWindowsRuntime -Directory $source

    foreach ($missing in @("LuxForge.dll", "nethost.dll", "DIRECT3D12/default.rootsig", "DIRECT3D12/luxscene.frag", "managed/LuxEngine.ScriptCore.dll")) {
        $path = Join-Path $source $missing
        $contents = [System.IO.File]::ReadAllText($path)
        Remove-Item -LiteralPath $path
        Expect-RuntimeFailure -Directory $source -Missing $missing
        [System.IO.File]::WriteAllText($path, "")
        Expect-RuntimeFailure -Directory $source -Missing $missing
        [System.IO.File]::WriteAllText($path, $contents)
    }

    $copy = Join-Path $testDirectory "copied runtime"
    Copy-LuxWindowsRuntime -Source $source -Destination $copy
    Assert-LuxWindowsRuntime -Directory $copy
    Check-File (Join-Path $copy "dxil.dll")
    Check-File (Join-Path $copy "managed/GameScripts.dll")
    Check-File (Join-Path $copy "managed/LuxEngine.ScriptCore.runtimeconfig.json")

    [System.IO.File]::WriteAllText((Join-Path $source "LuxPlayer.exe"), "mock player")
    $output = Join-Path $testDirectory "packages"
    & (Join-Path $repository "tools/PackageRuntime.ps1") -BuildDir (Join-Path $testDirectory "build") -OutputDir $output
    $runtimeZip = Join-Path $output "LuxPlayer-Portable-0.1.0-x64.zip"
    Check-File $runtimeZip
    $extracted = Join-Path $testDirectory "extracted runtime"
    Expand-Archive -LiteralPath $runtimeZip -DestinationPath $extracted
    Assert-LuxWindowsRuntime -Directory $extracted
    Check-File (Join-Path $extracted "scenes/ScenePlayer.lux")
    Check-File (Join-Path $extracted "licenses/LICENSE-LuxEngine.txt")
    Check-File (Join-Path $extracted "dxil.dll")
    Write-Host "Windows runtime manifests, missing-file rejection and portable archive contents passed."
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        Remove-Item -LiteralPath $testDirectory -Recurse -Force
    }
}

exit 0
