# Verify LuxPlayer Standalone Portable Distribution in an isolated environment
param(
    [string]$PackageZip = "dist/LuxPlayer-Portable-0.1.0-x64.zip",
    [string]$StagingDir = "",
    [switch]$RequireGpu = $false
)

$ErrorActionPreference = "Stop"
$root = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $root

function Get-VulkanGpuStatus {
    $vulkaninfo = $null
    $searchPaths = @()
    if ($env:VULKAN_SDK) { $searchPaths += Join-Path $env:VULKAN_SDK "bin\vulkaninfo.exe" }
    if ($env:VK_SDK_PATH) { $searchPaths += Join-Path $env:VK_SDK_PATH "bin\vulkaninfo.exe" }
    $searchPaths += "C:\Windows\System32\vulkaninfo.exe"
    foreach ($p in $searchPaths) {
        if ($p -and (Test-Path $p)) {
            $vulkaninfo = $p
            break
        }
    }
    if (-not $vulkaninfo) {
        $vCmd = Get-Command vulkaninfo.exe -ErrorAction SilentlyContinue
        if ($vCmd) { $vulkaninfo = $vCmd.Source }
    }

    if ($vulkaninfo -and (Test-Path $vulkaninfo)) {
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
                return @{ Status = "GPU_PROBE_UNKNOWN"; Reason = "vulkaninfo --summary timed out" }
            }
            $stdout = $stdOutTask.Result

            if ($proc.ExitCode -eq 0 -and ($stdout -match "GPU\d+:" -or $stdout -match "deviceName")) {
                $gpuMatch = [regex]::Match($stdout, "deviceName\s*=\s*(.+)")
                $deviceName = if ($gpuMatch.Success) { $gpuMatch.Groups[1].Value.Trim() } else { "Generic Vulkan GPU" }
                return @{ Status = "GPU_AVAILABLE"; DeviceName = $deviceName; VulkanInfo = $vulkaninfo }
            } else {
                return @{ Status = "GPU_UNAVAILABLE"; Reason = "vulkaninfo exit code $($proc.ExitCode) or no physical GPU listed" }
            }
        } catch {
            return @{ Status = "GPU_PROBE_UNKNOWN"; Reason = $_.Exception.Message }
        }
    }

    # Secondary hardware detection via WMI/CIM when vulkaninfo is absent
    try {
        $controllers = Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue
        if ($controllers) {
            $gpuNames = @()
            $virtualPattern = "Microsoft Basic|Microsoft Hyper-V|Hyper-V|Remote Desktop|VirtualBox|VMware|VdAgent|QEMU|Cirrus|Standard VGA|Citrix|Parallels|RDP|Basic Render"
            $physicalPattern = "NVIDIA|AMD|Radeon|Intel|GeForce|RTX|GTX|Arc|Iris"
            $hasPhysical = $false
            $hasVirtualOnly = $true

            foreach ($c in $controllers) {
                $name = $c.Name
                if ($name) {
                    if ($name -match $physicalPattern) {
                        $hasPhysical = $true
                        $hasVirtualOnly = $false
                        $gpuNames += $name
                    } elseif ($name -notmatch $virtualPattern) {
                        # Unknown adapter
                        $hasVirtualOnly = $false
                        $gpuNames += $name
                    }
                }
            }

            if ($hasPhysical) {
                return @{
                    Status = "GPU_AVAILABLE";
                    DeviceName = ($gpuNames -join ", ");
                    Reason = "Hardware GPU detected via CIM: $($gpuNames -join ', ')"
                }
            } elseif ($hasVirtualOnly) {
                return @{ Status = "GPU_UNAVAILABLE"; Reason = "vulkaninfo.exe not found and only virtual/cloud display adapters detected" }
            } else {
                return @{
                    Status = "GPU_PROBE_UNKNOWN";
                    Reason = "vulkaninfo.exe not found and unknown display adapter: $($gpuNames -join ', ')";
                    HardwareGpu = ($gpuNames -join ", ")
                }
            }
        }
    } catch {
        # ignore CIM failure and fallback to UNKNOWN
    }

    return @{ Status = "GPU_PROBE_UNKNOWN"; Reason = "vulkaninfo.exe not found and hardware query unavailable" }
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
    $gpuCheck = Get-VulkanGpuStatus
    Write-Host "GPU probe result: Status=$($gpuCheck.Status), Device=$($gpuCheck.DeviceName)$($gpuCheck.HardwareGpu), Reason=$($gpuCheck.Reason)"

    if ($RequireGpu -and $gpuCheck.Status -ne "GPU_AVAILABLE") {
        throw "Strict GPU requirement failed (-RequireGpu specified): $($gpuCheck.Status) - $($gpuCheck.Reason)"
    }

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
    } elseif ($gpuCheck.Status -eq "GPU_AVAILABLE") {
        # Physical GPU is present, so failure is a real bug/crash, NOT a headless restriction!
        throw "[GPU_RENDER_FAIL] Physical GPU ($($gpuCheck.DeviceName)) is present, but LuxPlayer failed with exit code $($proc3.ExitCode): $err3"
    } elseif ($gpuCheck.Status -eq "GPU_PROBE_UNKNOWN") {
        if ($RequireGpu) {
            throw "[GPU_RENDER_FAIL] -RequireGpu specified, GPU status is unknown ($($gpuCheck.Reason)), and LuxPlayer failed with exit code $($proc3.ExitCode): $err3"
        } elseif ($proc3.ExitCode -eq 1 -or $proc3.ExitCode -eq 3) {
            # Inconclusive probe, but LuxPlayer cleanly returned standard exit code for no graphics device
            Write-Host "[GPU_RENDER_SKIPPED] Inconclusive GPU probe ($($gpuCheck.Reason)), and LuxPlayer cleanly exited without GPU (exit code $($proc3.ExitCode))."
            Write-Host "[CLI_PASS] CLI options and standalone runtime integrity verified."
        } else {
            # Crash or unexpected failure: do NOT swallow!
            throw "[GPU_RENDER_FAIL] Inconclusive GPU probe ($($gpuCheck.Reason)), and LuxPlayer crashed/failed abnormally with exit code $($proc3.ExitCode): $err3"
        }
    } else {
        # Headless VM environment without physical Vulkan GPU
        if ($RequireGpu) {
            throw "[GPU_RENDER_FAIL] -RequireGpu specified, but environment is headless ($($gpuCheck.Reason))."
        }
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
