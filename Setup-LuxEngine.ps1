# LuxEngine one-time migration for the user's existing AsterEngineStarter.
# Creates a separate LuxEngine project; never modifies or deletes the original.
$ErrorActionPreference = 'Stop'
$source = 'C:/Users/eunso/Desktop/LuxEngine/AsterEngineStarter'
$target = 'C:/Users/eunso/Desktop/LuxEngine/LuxEngine'
$devenv = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/devenv.exe'

if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
    throw "Original project not found: $source"
}
if (Test-Path -LiteralPath $target) {
    throw "Destination already exists: $target. Nothing was overwritten."
}
if (-not (Test-Path -LiteralPath $devenv)) {
    throw "Visual Studio 2026 not found: $devenv"
}

Write-Host '[1/5] Copying project source without old build outputs...'
New-Item -ItemType Directory -Path $target -Force | Out-Null
$excluded = @('build', 'out', '.vs', '.git', '.idea')
Get-ChildItem -LiteralPath $source -Force | Where-Object { $excluded -notcontains $_.Name } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $target $_.Name) -Recurse -Force
}

Write-Host '[2/5] Renaming CMake targets, C++ namespace, include paths, window title and docs...'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$textExt = @('.txt', '.md', '.cpp', '.cc', '.cxx', '.hpp', '.h', '.hxx', '.cmake', '.json', '.in', '.yml', '.yaml', '.toml', '.ps1')
Get-ChildItem -LiteralPath $target -Recurse -File -Force | Where-Object { $textExt -contains $_.Extension -or $_.Name -eq 'CMakeLists.txt' } | ForEach-Object {
    $original = [System.IO.File]::ReadAllText($_.FullName, [System.Text.Encoding]::UTF8)
    $updated = $original.Replace('AsterEngineStarter', 'LuxEngine')
    $updated = $updated.Replace('AsterSandbox', 'LuxEngineDemo')
    $updated = $updated.Replace('AsterEngine', 'LuxEngine')
    $updated = $updated.Replace('Aster Engine', 'LuxEngine')
    $updated = [regex]::Replace($updated, '\bAster\b', 'LuxEngine')
    $updated = $updated.Replace('Visual Studio 17 2022', 'Visual Studio 18 2026')
    if ($updated -cne $original) {
        [System.IO.File]::WriteAllText($_.FullName, $updated, $utf8)
    }
}
$oldIncludes = Join-Path $target 'Engine/include/Aster'
if (Test-Path -LiteralPath $oldIncludes) {
    Rename-Item -LiteralPath $oldIncludes -NewName 'LuxEngine'
}

Write-Host '[3/5] Configuring Visual Studio 2026 CMake solution...'
& cmake -S $target -B (Join-Path $target 'build') -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

Write-Host '[4/5] Compiling LuxEngine and LuxEngineDemo...'
& cmake --build (Join-Path $target 'build') --config Debug
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }

Write-Host '[5/5] Creating Visual Studio 2026 desktop shortcut...'
$solution = Join-Path $target 'build/LuxEngine.slnx'
$exe = Join-Path $target 'build/bin/Debug/LuxEngineDemo.exe'
if (-not (Test-Path -LiteralPath $solution)) { throw "Solution missing: $solution" }
if (-not (Test-Path -LiteralPath $exe)) { throw "Executable missing: $exe" }
$shell = New-Object -ComObject WScript.Shell
$desktop = [Environment]::GetFolderPath('Desktop')
$lnk = Join-Path $desktop 'LuxEngine.lnk'
$shortcut = $shell.CreateShortcut($lnk)
$shortcut.TargetPath = $devenv
$shortcut.Arguments = '"' + $solution + '"'
$shortcut.WorkingDirectory = $target
$shortcut.IconLocation = $devenv + ',0'
$shortcut.Description = 'LuxEngine - Visual Studio 2026'
$shortcut.Save()

# Delete ONLY an old shortcut that points at this particular Aster project.
$oldLnk = Join-Path $desktop 'Aster Engine.lnk'
if (Test-Path -LiteralPath $oldLnk) {
    $oldLinkInfo = $shell.CreateShortcut($oldLnk)
    if ($oldLinkInfo.Arguments -like '*AsterEngineStarter*') {
        Remove-Item -LiteralPath $oldLnk
        Write-Host 'Removed old Aster Engine desktop shortcut.'
    }
}

Write-Host ''
Write-Host 'SUCCESS: LuxEngine project, demo and desktop shortcut are ready.' -ForegroundColor Green
Write-Host "Project: $target"
Write-Host "Solution: $solution"
Write-Host "Executable: $exe"
Write-Host "Shortcut: $lnk"
Write-Host 'Original AsterEngineStarter is untouched and can serve as a backup.'


