# Verify LuxPlayer Standalone Portable Distribution in an isolated environment
param(
    [string]$PackageZip = "dist/LuxPlayer-Portable-0.1.0-x64.zip",
    [string]$StagingDir = ""
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

function Test-VulkanGpuAvailable {
    $vulkaninfo = $null
    if ($env:VULKAN_SDK -and (Test-Path (Join-Path $env:VULKAN_SDK "bin\vulkaninfo.exe"))) {
        $vulkaninfo = Join-Path $env:VULKAN_SDK "bin\vulkaninfo.exe"
    } elseif (Test-Path "C:\Windows\System32\vulkaninfo.exe") {
        $vulkaninfo = "C:\Windows\System32\vulkaninfo.exe"
    } else {
        $vCmd = Get-Command vulkaninfo.exe -ErrorAction SilentlyContinue
        if ($vCmd) { $vulkaninfo = $vCmd.Source }
    }

    if (-not $vulkaninfo -or -not (Test-Path $vulkaninfo)) {
        return @{ Available = $false; Reason = "vulkaninfo.exe not found" }
    }

    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $vulkaninfo
        $psi.Arguments = "--summary"
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = $true

        $proc = [System.Diagnostics.Process]::Start($psi)
        $stdOutTask = $proc.StandardOutput.ReadToEndAsync()
        $stdErrTask = $proc.StandardError.ReadToEndAsync()
        if (-not $proc.WaitForExit(10000)) {
            $proc.Kill()
            return @{ Available = $false; Reason = "vulkaninfo --summary timed out" }
        }
        $stdout = $stdOutTask.Result

        if ($proc.ExitCode -eq 0 -and ($stdout -match "GPU\d+:" -or $stdout -match "deviceName")) {
            $gpuMatch = [regex]::Match($stdout, "deviceName\s*=\s*(.+)")
            $deviceName = if ($gpuMatch.Success) { $gpuMatch.Groups[1].Value.Trim() } else { "Generic Vulkan GPU" }
            return @{ Available = $true; DeviceName = $deviceName }
        } else {
            return @{ Available = $false; Reason = "vulkaninfo exit code $($proc.ExitCode) or no physical GPU listed" }
        }
    } catch {
        return @{ Available = $false; Reason = $_.Exception.Message }
    }
}

Write-Host "=== Testing LuxPlayer Standalone Portable Package ==="

$testStaging = $StagingDir
$cleanup = $false
if ([string]::IsNullOrWhiteSpace($testStaging)) {
    $zipPath = Join-Path $root $PackageZip
    if (-not (Test-Path $zipPath)) {
        throw "Runtime package zip not found: $zipPath. Run PackageRuntime.ps1 first."
    }
    $testStaging = Join-Path ([System.IO.Path]::GetTempPath()) "LuxPlayerTest Path $(Get-Random)"
    New-Item -ItemType Directory -Path $testStaging -Force | Out-Null
    Write-Host "Extracting $zipPath to $testStaging..."
    Expand-Archive -Path $zipPath -DestinationPath $testStaging -Force
    $cleanup = $true
}

try {
    $playerExe = Join-Path $testStaging "LuxPlayer.exe"
    if (-not (Test-Path $playerExe)) {
        throw "LuxPlayer.exe not found in $testStaging"
    }

    # Verify essential dependencies inside package
    $essentialFiles = @(
        "SDL3.dll",
        "shaders/Basic.vert.spv",
        "shaders/Basic.frag.spv",
        "scenes/Demo.lux",
        "licenses/LICENSE-LuxEngine.txt",
        "licenses/DEPENDENCIES.md"
    )
    foreach ($f in $essentialFiles) {
        $p = Join-Path $testStaging $f
        if (-not (Test-Path $p)) {
            throw "Package missing essential file: $f"
        }
    }

    # Sanitize PATH: retain standard Windows System32 and Vulkan SDK bin if present
    $cleanPath = "C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
    if ($env:VULKAN_SDK -and (Test-Path (Join-Path $env:VULKAN_SDK "bin"))) {
        $cleanPath = "$cleanPath;$env:VULKAN_SDK\bin"
    } elseif ($env:VK_SDK_PATH -and (Test-Path (Join-Path $env:VK_SDK_PATH "bin"))) {
        $cleanPath = "$cleanPath;$env:VK_SDK_PATH\bin"
    }

    # 1. Test 'LuxPlayer.exe --version'
    Write-Host "1. Testing 'LuxPlayer.exe --version' in clean environment..."
    $psi1 = New-Object System.Diagnostics.ProcessStartInfo
    $psi1.FileName = $playerExe
    $psi1.Arguments = "--version"
    $psi1.WorkingDirectory = $testStaging
    $psi1.EnvironmentVariables["PATH"] = $cleanPath
    $psi1.UseShellExecute = $false
    $psi1.RedirectStandardOutput = $true
    $psi1.RedirectStandardError = $true

    $proc1 = [System.Diagnostics.Process]::Start($psi1)
    $stdOutTask1 = $proc1.StandardOutput.ReadToEndAsync()
    $stdErrTask1 = $proc1.StandardError.ReadToEndAsync()
    if (-not $proc1.WaitForExit(10000)) {
        $proc1.Kill()
        throw "LuxPlayer.exe --version timed out after 10 seconds"
    }
    $output1 = $stdOutTask1.Result
    $err1 = $stdErrTask1.Result
    Write-Host "Version output: $output1"
    if ($proc1.ExitCode -ne 0 -or -not ($output1 -like "*LuxPlayer version*")) {
        throw "LuxPlayer.exe --version failed with exit code $($proc1.ExitCode): $err1"
    }

    # 2. Test 'LuxPlayer.exe --help'
    Write-Host "2. Testing 'LuxPlayer.exe --help' in clean environment..."
    $psi2 = New-Object System.Diagnostics.ProcessStartInfo
    $psi2.FileName = $playerExe
    $psi2.Arguments = "--help"
    $psi2.WorkingDirectory = $testStaging
    $psi2.EnvironmentVariables["PATH"] = $cleanPath
    $psi2.UseShellExecute = $false
    $psi2.RedirectStandardOutput = $true
    $psi2.RedirectStandardError = $true

    $proc2 = [System.Diagnostics.Process]::Start($psi2)
    $stdOutTask2 = $proc2.StandardOutput.ReadToEndAsync()
    $stdErrTask2 = $proc2.StandardError.ReadToEndAsync()
    if (-not $proc2.WaitForExit(10000)) {
        $proc2.Kill()
        throw "LuxPlayer.exe --help timed out after 10 seconds"
    }
    $output2 = $stdOutTask2.Result
    $err2 = $stdErrTask2.Result
    Write-Host "Help output: $output2"
    if ($proc2.ExitCode -ne 0 -or -not ($output2 -like "*usage: LuxPlayer*")) {
        throw "LuxPlayer.exe --help failed with exit code $($proc2.ExitCode): $err2"
    }

    # 3. Test 'LuxPlayer.exe scenes/Demo.lux --frames 5'
    Write-Host "3. Testing 'LuxPlayer.exe scenes/Demo.lux --frames 5'..."
    $gpuCheck = Test-VulkanGpuAvailable
    Write-Host "GPU probe result: Available=$($gpuCheck.Available), Details=$($gpuCheck.DeviceName)$($gpuCheck.Reason)"

    $psi3 = New-Object System.Diagnostics.ProcessStartInfo
    $psi3.FileName = $playerExe
    $psi3.Arguments = "scenes/Demo.lux --frames 5"
    $psi3.WorkingDirectory = $testStaging
    $psi3.EnvironmentVariables["PATH"] = $cleanPath
    $psi3.UseShellExecute = $false
    $psi3.RedirectStandardOutput = $true
    $psi3.RedirectStandardError = $true

    $proc3 = [System.Diagnostics.Process]::Start($psi3)
    $stdOutTask3 = $proc3.StandardOutput.ReadToEndAsync()
    $stdErrTask3 = $proc3.StandardError.ReadToEndAsync()
    if (-not $proc3.WaitForExit(20000)) {
        $proc3.Kill()
        throw "LuxPlayer.exe scenes/Demo.lux timed out after 20 seconds"
    }
    $output3 = $stdOutTask3.Result
    $err3 = $stdErrTask3.Result
    Write-Host "Player run output:`n$output3"
    if ($err3) { Write-Host "Player run stderr:`n$err3" }

    $gpuRenderPassed = $false
    if ($proc3.ExitCode -eq 0) {
        $gpuRenderPassed = $true
        Write-Host "[GPU_RENDER_PASS] LuxPlayer successfully rendered frames."
    } elseif ($gpuCheck.Available) {
        # Physical GPU is present, so failure is a real bug/crash, NOT a headless restriction!
        throw "[GPU_RENDER_FAIL] Physical GPU ($($gpuCheck.DeviceName)) is present, but LuxPlayer failed with exit code $($proc3.ExitCode): $err3"
    } else {
        # Headless VM environment without physical Vulkan GPU
        Write-Host "[GPU_RENDER_SKIPPED] Headless environment without physical GPU detected ($($gpuCheck.Reason), exit code $($proc3.ExitCode))."
        Write-Host "[CLI_PASS] CLI options and standalone runtime integrity verified."
    }

    if ($gpuRenderPassed) {
        Write-Host "`n=== Standalone LuxPlayer Verification: ALL PASSED (CLI + GPU Render) ==="
    } else {
        Write-Host "`n=== Standalone LuxPlayer Verification: CLI PASSED (GPU Render SKIPPED - Headless) ==="
    }
}
finally {
    if ($cleanup -and (Test-Path $testStaging)) {
        Remove-Item -Recurse -Force $testStaging -ErrorAction SilentlyContinue
    }
}
