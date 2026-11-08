# Verify LuxEditor Standalone Portable Distribution in an isolated environment
param(
    [string]$PackageZip = "dist/LuxEditor-Portable-0.1.0-x64.zip",
    [string]$StagingDir = ""
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

Write-Host "=== Testing LuxEditor Standalone Portable Package ==="

$testStaging = $StagingDir
$cleanup = $false
if ([string]::IsNullOrWhiteSpace($testStaging)) {
    $zipPath = Join-Path $root $PackageZip
    if (-not (Test-Path $zipPath)) {
        throw "Editor package zip not found: $zipPath. Run PackageEditor.ps1 first."
    }
    $testStaging = Join-Path ([System.IO.Path]::GetTempPath()) "LuxEditorTest-$(Get-Random)"
    New-Item -ItemType Directory -Path $testStaging -Force | Out-Null
    Write-Host "Extracting $zipPath to $testStaging..."
    Expand-Archive -Path $zipPath -DestinationPath $testStaging -Force
    $cleanup = $true
}

try {
    $editorExe = Join-Path $testStaging "LuxEditor.exe"
    if (-not (Test-Path $editorExe)) {
        throw "LuxEditor.exe not found in $testStaging"
    }

    # Verify essential dependencies inside package
    $essentialDlls = @("SDL3.dll", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms/qwindows.dll")
    foreach ($dll in $essentialDlls) {
        $p = Join-Path $testStaging $dll
        if (-not (Test-Path $p)) {
            throw "Package missing essential DLL/plugin: $dll"
        }
    }

    Write-Host "1. Testing 'LuxEditor.exe --version'..."
    # Sanitize PATH: retain only Windows System32 so host Qt/vcpkg cannot interfere
    $cleanPath = "C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $editorExe
    $psi.Arguments = "--version"
    $psi.WorkingDirectory = $testStaging
    $psi.EnvironmentVariables["PATH"] = $cleanPath
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdOutTask1 = $proc.StandardOutput.ReadToEndAsync()
    $stdErrTask1 = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit(10000)) {
        $proc.Kill()
        throw "LuxEditor.exe --version timed out after 10 seconds"
    }
    $output = $stdOutTask1.Result
    $err = $stdErrTask1.Result
    Write-Host "Version output: $output"
    if ($proc.ExitCode -ne 0) {
        throw "LuxEditor.exe --version failed with exit code $($proc.ExitCode): $err"
    }

    Write-Host "2. Testing 'LuxEditor.exe --smoke-test' in clean environment..."
    $psi2 = New-Object System.Diagnostics.ProcessStartInfo
    $psi2.FileName = $editorExe
    $psi2.Arguments = "--smoke-test"
    $psi2.WorkingDirectory = $testStaging
    $psi2.EnvironmentVariables["PATH"] = $cleanPath
    $psi2.EnvironmentVariables["QT_QPA_PLATFORM"] = "offscreen"
    $psi2.UseShellExecute = $false
    $psi2.RedirectStandardOutput = $true
    $psi2.RedirectStandardError = $true

    $proc2 = [System.Diagnostics.Process]::Start($psi2)
    $stdOutTask = $proc2.StandardOutput.ReadToEndAsync()
    $stdErrTask = $proc2.StandardError.ReadToEndAsync()
    if (-not $proc2.WaitForExit(20000)) {
        $proc2.Kill()
        throw "LuxEditor.exe --smoke-test timed out after 20 seconds"
    }
    $output2 = $stdOutTask.Result
    $err2 = $stdErrTask.Result
    Write-Host "Smoke test output: $output2"
    if ($proc2.ExitCode -ne 0 -or -not ($output2 -like "*Smoke test passed*")) {
        throw "LuxEditor.exe --smoke-test failed with exit code $($proc2.ExitCode): $err2"
    }

    Write-Host "`n=== Standalone LuxEditor Verification: ALL PASSED ==="
}
finally {
    if ($cleanup -and (Test-Path $testStaging)) {
        Remove-Item -Recurse -Force $testStaging -ErrorAction SilentlyContinue
    }
}
