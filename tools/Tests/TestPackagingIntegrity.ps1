# Packaging must reject the wrong configuration and Debug CRTs in Release output.
param([string]$BuildDir = "build/local")

$ErrorActionPreference = "Stop"
$repository = (Get-Item $PSScriptRoot).Parent.Parent.FullName
$powerShell = (Get-Process -Id $PID).Path
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "LuxPackagingIntegrity-$([guid]::NewGuid())"

function Expect-PackagingFailure {
    param([string]$Script, [string[]]$Arguments, [string]$Expected)
    # Keep expected child-process errors as data, then check the actual diagnostic.
    $ErrorActionPreference = "Continue"
    $output = & $powerShell -NoProfile -File (Join-Path $repository "tools/$Script") @Arguments 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or $output -notmatch $Expected) {
        throw "Expected '$Expected' from $Script, exit $LASTEXITCODE. Output: $output"
    }
}

try {
    New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
    $outputDirectory = Join-Path $testDirectory "packages"
    foreach ($script in @("PackageRuntime.ps1", "PackageEditor.ps1")) {
        Expect-PackagingFailure -Script $script -Arguments @(
            "-BuildDir", $BuildDir, "-OutputDir", $outputDirectory,
            "-Configuration", "NonExistentConfig_xyz"
        ) -Expected "Unsupported configuration"
    }
    Expect-PackagingFailure -Script "PackageSdk.ps1" -Arguments @(
        "-BuildDir", (Join-Path $testDirectory "missing-build"), "-OutputDir", $outputDirectory
    ) -Expected "cmake --install (Debug|Release) failed"

    $mockBuild = Join-Path $testDirectory "build"
    $mockBin = Join-Path $mockBuild "bin/Release"
    New-Item -ItemType Directory -Path $mockBin -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $mockBin "LuxPlayer.exe"), "mock player")
    [System.IO.File]::WriteAllText((Join-Path $mockBin "LuxEditor.exe"), "mock editor")
    [System.IO.File]::WriteAllText((Join-Path $mockBin "ucrtbased.dll"), "mock Debug CRT")
    foreach ($script in @("PackageRuntime.ps1", "PackageEditor.ps1")) {
        Expect-PackagingFailure -Script $script -Arguments @(
            "-BuildDir", $mockBuild, "-OutputDir", $outputDirectory, "-Configuration", "Release"
        ) -Expected "Debug runtime DLLs detected"
    }
    Write-Host "Packaging configuration rejection, missing-build diagnostics and Debug CRT checks passed."
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        Remove-Item -LiteralPath $testDirectory -Recurse -Force
    }
}

# GitHub's pwsh wrapper forwards LASTEXITCODE, including expected child failures.
exit 0
