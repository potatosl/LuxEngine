# Reproduce the GitHub Actions pwsh wrapper instead of relying on -File exit defaults.
$ErrorActionPreference = "Stop"
$repository = (Get-Item $PSScriptRoot).Parent.Parent.FullName
$powerShell = (Get-Process -Id $PID).Path
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "LuxPwshExitCodes-$([guid]::NewGuid())"

function Invoke-GitHubPowerShellWrapper {
    param([string]$Script)

    $command = @'
$ErrorActionPreference = 'Stop'
$global:LASTEXITCODE = 29
& $env:LUX_TEST_SCRIPT_PATH
if ((Test-Path -LiteralPath variable:\LASTEXITCODE)) { exit $LASTEXITCODE }
'@
    $processInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $powerShell
    $processInfo.WorkingDirectory = $repository
    $processInfo.UseShellExecute = $false
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $processInfo.Environment["LUX_TEST_SCRIPT_PATH"] = $Script
    $processInfo.ArgumentList.Add("-NoProfile")
    $processInfo.ArgumentList.Add("-EncodedCommand")
    $processInfo.ArgumentList.Add([Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($command)))
    $process = [System.Diagnostics.Process]::Start($processInfo)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            $process.Kill($true)
            throw "GitHub pwsh wrapper timed out: $Script"
        }
        return @{ ExitCode = $process.ExitCode; Output = $stdout.Result + $stderr.Result }
    }
    finally {
        $process.Dispose()
    }
}

try {
    foreach ($case in @(
        @{ Script = "TestPackagingIntegrity.ps1"; Success = "Debug CRT checks passed" },
        @{ Script = "TestSdkRelocationSafety.ps1"; Success = "preserved both files" },
        @{ Script = "TestWindowsRuntimePackaging.ps1"; Success = "portable archive contents passed" }
    )) {
        $result = Invoke-GitHubPowerShellWrapper -Script (Join-Path $PSScriptRoot $case.Script)
        if ($result.ExitCode -ne 0 -or $result.Output -notmatch $case.Success) {
            throw "GitHub pwsh wrapper failed for $($case.Script), exit $($result.ExitCode). Output: $($result.Output)"
        }
    }

    New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
    $assertionFailure = Join-Path $testDirectory "AssertionFailure.ps1"
    [System.IO.File]::WriteAllText($assertionFailure, "throw 'Intentional assertion failure'")
    $result = Invoke-GitHubPowerShellWrapper -Script $assertionFailure
    if ($result.ExitCode -eq 0 -or $result.Output -notmatch "Intentional assertion failure") {
        throw "The wrapper hid a real assertion failure."
    }
    $explicitFailure = Join-Path $testDirectory "ExplicitFailure.ps1"
    [System.IO.File]::WriteAllText($explicitFailure, "exit 17")
    $result = Invoke-GitHubPowerShellWrapper -Script $explicitFailure
    if ($result.ExitCode -ne 17) {
        throw "The wrapper did not preserve explicit failure exit code 17: $($result.ExitCode)"
    }
    Write-Host "GitHub pwsh wrapper accepts successful tests and preserves real failures."
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        Remove-Item -LiteralPath $testDirectory -Recurse -Force
    }
}

exit 0
