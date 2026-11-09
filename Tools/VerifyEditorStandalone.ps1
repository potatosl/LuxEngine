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
    $koreanPart = [string]::new([char[]]@(0xD55C, 0xAE00, 0xD14C, 0xC2A4, 0xD2B8)) # "한글테스트"
    $testStaging = Join-Path ([System.IO.Path]::GetTempPath()) "LuxEditorTest $koreanPart $(Get-Random)"
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
    $essentialFiles = @(
        "SDL3.dll",
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Widgets.dll",
        "platforms/qwindows.dll",
        "shaders/Basic.vert.spv",
        "shaders/Basic.frag.spv",
        "licenses/LICENSE-LuxEngine.txt",
        "licenses/LICENSE-Qt6.txt",
        "licenses/LICENSE-LGPLv3.txt",
        "licenses/DEPENDENCIES.md"
    )
    foreach ($f in $essentialFiles) {
        $p = Join-Path $testStaging $f
        if (-not (Test-Path $p)) {
            throw "Package missing essential file/dependency: $f"
        }
    }

    # Sanitize PATH: retain only Windows System32 so host Qt/vcpkg cannot interfere
    $cleanPath = "C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"

    # 1. Test 'LuxEditor.exe --version'
    Write-Host "1. Testing 'LuxEditor.exe --version'..."
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
    if ($err) { Write-Host "Version stderr: $err" }
    if ($proc.ExitCode -ne 0 -or -not ($output -like "*LuxEditor*")) {
        throw "LuxEditor.exe --version failed with exit code $($proc.ExitCode): $err (output: $output)"
    }

    # 2. Test 'LuxEditor.exe --help'
    Write-Host "2. Testing 'LuxEditor.exe --help'..."
    $psiHelp = New-Object System.Diagnostics.ProcessStartInfo
    $psiHelp.FileName = $editorExe
    $psiHelp.Arguments = "--help"
    $psiHelp.WorkingDirectory = $testStaging
    $psiHelp.EnvironmentVariables["PATH"] = $cleanPath
    $psiHelp.UseShellExecute = $false
    $psiHelp.RedirectStandardOutput = $true
    $psiHelp.RedirectStandardError = $true

    $procHelp = [System.Diagnostics.Process]::Start($psiHelp)
    $stdOutTaskHelp = $procHelp.StandardOutput.ReadToEndAsync()
    $stdErrTaskHelp = $procHelp.StandardError.ReadToEndAsync()
    if (-not $procHelp.WaitForExit(10000)) {
        $procHelp.Kill()
        throw "LuxEditor.exe --help timed out after 10 seconds"
    }
    $outputHelp = $stdOutTaskHelp.Result
    Write-Host "Help output: $outputHelp"
    if ($procHelp.ExitCode -ne 0 -or -not ($outputHelp -like "*Usage: LuxEditor.exe*")) {
        throw "LuxEditor.exe --help failed with exit code $($procHelp.ExitCode)"
    }

    # 3. Test 'LuxEditor.exe --smoke-test' in clean environment
    Write-Host "3. Testing 'LuxEditor.exe --smoke-test' in clean environment..."
    $psi2 = New-Object System.Diagnostics.ProcessStartInfo
    $psi2.FileName = $editorExe
    $psi2.Arguments = "--smoke-test"
    $psi2.WorkingDirectory = $testStaging
    $psi2.EnvironmentVariables["PATH"] = $cleanPath
    if (Test-Path (Join-Path $testStaging "platforms/qoffscreen.dll")) {
        $psi2.EnvironmentVariables["QT_QPA_PLATFORM"] = "offscreen"
    }
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
    if ($err2) { Write-Host "Smoke test stderr: $err2" }
    if ($proc2.ExitCode -ne 0 -or -not ($output2 -like "*Smoke test passed*")) {
        throw "LuxEditor.exe --smoke-test failed with exit code $($proc2.ExitCode): $err2 (output: $output2)"
    }

    Write-Host "`n=== Standalone LuxEditor Verification: ALL PASSED ==="
}
finally {
    if ($cleanup -and (Test-Path $testStaging)) {
        Remove-Item -Recurse -Force $testStaging -ErrorAction SilentlyContinue
    }
}
