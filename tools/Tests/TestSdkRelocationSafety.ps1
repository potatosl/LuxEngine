# Verify SDK relocation rejects existing directories without changing user data.
param([string]$BuildDir = "build/local")

$ErrorActionPreference = "Stop"
$repository = (Get-Item $PSScriptRoot).Parent.Parent.FullName
$powerShell = (Get-Process -Id $PID).Path
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "LuxUserDataSafety-$([guid]::NewGuid())"

function Invoke-ExpectedRelocationFailure {
    $ErrorActionPreference = "Continue"
    $output = & $powerShell -NoProfile -File (Join-Path $repository "tools/VerifySdkRelocation.ps1") -BuildDir $BuildDir -SdkPrefix $testDirectory 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -or $output -notmatch "Target SdkPrefix directory already exists and is not empty") {
        throw "Relocation did not reject existing user data as expected, exit $LASTEXITCODE. Output: $output"
    }
}

try {
    New-Item -ItemType Directory -Path (Join-Path $testDirectory "subproject") -Force | Out-Null
    $userFile = Join-Path $testDirectory "important_user_work.txt"
    $sourceFile = Join-Path $testDirectory "subproject/code.cpp"
    [System.IO.File]::WriteAllText($userFile, "CRITICAL USER DATA - DO NOT DELETE")
    [System.IO.File]::WriteAllText($sourceFile, "// User code")
    $userHash = (Get-FileHash -LiteralPath $userFile -Algorithm SHA256).Hash
    $sourceHash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash

    Invoke-ExpectedRelocationFailure
    foreach ($file in @($userFile, $sourceFile)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "SDK relocation deleted a user file: $file"
        }
    }
    if ((Get-FileHash -LiteralPath $userFile -Algorithm SHA256).Hash -ne $userHash -or
        (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash -ne $sourceHash) {
        throw "SDK relocation changed existing user data."
    }
    Write-Host "SDK relocation rejected existing user data and preserved both files."
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        Remove-Item -LiteralPath $testDirectory -Recurse -Force
    }
}

# A rejected child invocation is expected; the test itself completed successfully.
exit 0
