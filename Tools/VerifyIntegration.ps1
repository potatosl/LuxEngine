# LuxEngine integration verify: engine build + tests, then editor build. Logs to build/verify-log.txt
$ErrorActionPreference = "Continue"
$root = (Get-Item $PSScriptRoot).Parent.FullName
$logDir = Join-Path $root "build"
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }
$log = Join-Path $logDir "verify-log.txt"
"=== START $(Get-Date -Format s) ===" | Out-File $log -Encoding utf8
function L($m) { $m | Out-File $log -Append -Encoding utf8; Write-Host $m }
function Run($name, [scriptblock]$cmd) {
  L "`n=== STEP: $name ==="
  & $cmd 2>&1 | ForEach-Object { "$_" } | Out-File $log -Append -Encoding utf8
  $code = $LASTEXITCODE; L "=== EXIT [$name]: $code ==="; return $code
}
Set-Location $root
L "git: $(git rev-parse --abbrev-ref HEAD) $(git rev-parse --short HEAD)"
$vsCmake = "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$cmake = if (Test-Path "$root/Tools/cmake/bin/cmake.exe") { "$root/Tools/cmake/bin/cmake.exe" } elseif (Test-Path $vsCmake) { $vsCmake } else { (Get-Command cmake).Source }
$ctest = Join-Path (Split-Path $cmake) "ctest.exe"
L "cmake: $cmake"
L "VULKAN_SDK=$env:VULKAN_SDK  VCPKG_ROOT=$env:VCPKG_ROOT  CMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH  Qt6_DIR=$env:Qt6_DIR"
L "Qt dirs: $((Get-ChildItem C:\Qt -Directory -ErrorAction SilentlyContinue | Select-Object -Expand FullName) -join ', ')"
L "vcpkg: $((Get-Command vcpkg -ErrorAction SilentlyContinue).Source)"
$b = "$root/build/verify"
$tc = @()
if (Test-Path "C:/vcpkg/scripts/buildsystems/vcpkg.cmake") { $tc = @("-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake", "-DVCPKG_TARGET_TRIPLET=x64-windows", "-DCMAKE_PREFIX_PATH=C:/LuxDeps") }
L "toolchain args: $tc"

# Select generator (VS 2026 if available, otherwise VS 2022 or default)
$genArgs = @()
if (Test-Path "C:/Program Files/Microsoft Visual Studio/18") {
    $genArgs = @("-G", "Visual Studio 18 2026", "-A", "x64")
} elseif (Test-Path "C:/Program Files/Microsoft Visual Studio/2022") {
    $genArgs = @("-G", "Visual Studio 17 2022", "-A", "x64")
}

# SDL3.dll was replaced by the Vulkan-enabled build; deleting bin forces the post-build copy to run again.
if (Test-Path "$b/bin") { Remove-Item "$b/bin" -Recurse -Force -ErrorAction SilentlyContinue }
$c1 = Run "configure engine" { & $cmake -S $root -B $b @genArgs -DLUX_BUILD_TESTS=ON -DLUX_BUILD_EDITOR=OFF @tc }
if ($c1 -eq 0) {
  $c2 = Run "build engine" { & $cmake --build $b --config Debug -- /m /v:minimal }
  if ($c2 -eq 0) {
    Run "ctest" { & $ctest --test-dir $b -C Debug --output-on-failure --timeout 180 } | Out-Null
  }
}
# Games built from the editor find the engine through this installed SDK (LUX_ENGINE_DIR).
# A separate engine-only build provides both Debug and Release libraries (Debug ones end in "d").
$sdkBuild = "$root/build/sdk"
$sdk = "C:/LuxSDK"
$s1 = Run "configure SDK" { & $cmake -S $root -B $sdkBuild @genArgs -DLUX_BUILD_TESTS=OFF -DLUX_BUILD_EDITOR=OFF @tc }
if ($s1 -eq 0) {
  $s2 = Run "build SDK Debug" { & $cmake --build $sdkBuild --config Debug -- /m /v:minimal }
  $s3 = Run "build SDK Release" { & $cmake --build $sdkBuild --config Release -- /m /v:minimal }
  if ($s2 -eq 0 -and $s3 -eq 0) {
    if (Test-Path $sdk) { Remove-Item $sdk -Recurse -Force -ErrorAction SilentlyContinue }
    $i1 = Run "install SDK Debug" { & $cmake --install $sdkBuild --config Debug --prefix $sdk }
    $i2 = Run "install SDK Release" { & $cmake --install $sdkBuild --config Release --prefix $sdk }
    if ($i1 -eq 0 -and $i2 -eq 0) {
      [Environment]::SetEnvironmentVariable("LUX_ENGINE_DIR", $sdk, "User"); $env:LUX_ENGINE_DIR = $sdk; L "LUX_ENGINE_DIR=$sdk"
      L "SDK libs: $((Get-ChildItem "$sdk/lib" -Filter *.lib | Select-Object -Expand Name) -join ', ')"
    }
  }
}
$c3 = Run "configure editor" { & $cmake -S $root -B $b -DLUX_BUILD_EDITOR=ON @tc }
if ($c3 -eq 0) {
  $c4 = Run "build editor" { & $cmake --build $b --config Debug -- /m /v:minimal }
  if ($c4 -eq 0) { Run "ctest editor" { & $ctest --test-dir $b -C Debug --output-on-failure -R Editor --timeout 1800 } | Out-Null }
}
# Release build and tests of everything (engine, runtime, editor).
$r1 = Run "build all Release" { & $cmake --build $b --config Release -- /m /v:minimal }
if ($r1 -eq 0) { Run "ctest Release" { & $ctest --test-dir $b -C Release --output-on-failure --timeout 1800 } | Out-Null }
# The editor on its own, built against the installed SDK (how it is built outside this repo).
$eb = "$root/build/editor-standalone"
$e1 = Run "configure standalone editor" { & $cmake -S "$root/Editor" -B $eb @genArgs -DLUX_BUILD_TESTS=ON "-DLuxEngine_DIR=C:/LuxSDK/lib/cmake/LuxEngine" @tc }
if ($e1 -eq 0) {
  $e2 = Run "build standalone editor" { & $cmake --build $eb --config Debug -- /m /v:minimal }
  if ($e2 -eq 0) {
    Run "ctest standalone editor" { & $ctest --test-dir $eb -C Debug --output-on-failure --timeout 1800 } | Out-Null
    L "standalone bin: $((Get-ChildItem "$eb/bin/Debug" -Recurse -Include *.exe,qwindows*.dll,*.spv | Select-Object -Expand Name) -join ', ')"
  }
}
# A clean export of the commit (no ignored or untracked files) must configure, so nothing needed is missing from git.
$fresh = "$root/build/fresh-src"
if (Test-Path $fresh) { Remove-Item $fresh -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory $fresh | Out-Null
Run "export HEAD" { cmd /c "git -C `"$root`" archive --format=tar HEAD | tar -x -C `"$fresh`"" } | Out-Null
Run "configure fresh export" { & $cmake -S $fresh -B "$fresh/_b" @genArgs -DLUX_BUILD_TESTS=ON -DLUX_BUILD_EDITOR=ON @tc } | Out-Null
L "`n=== DONE $(Get-Date -Format s) ==="
