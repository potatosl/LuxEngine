# Shared Windows runtime contract for portable packages and SDK consumers.
function Get-LuxWindowsRuntimeFiles {
    @(
        "SDL3.dll",
        "LuxForge.dll",
        "amd_ags_x64.dll",
        "dxcompiler.dll",
        "WinPixEventRuntime.dll",
        "gpu.data",
        "gpu.cfg",
        "nethost.dll",
        "managed/LuxEngine.ScriptCore.dll",
        "managed/LuxEngine.ScriptCore.runtimeconfig.json",
        "DIRECT3D12/default.rootsig",
        "DIRECT3D12/compute.rootsig",
        "DIRECT3D12/lux2d.vert",
        "DIRECT3D12/lux2d.frag",
        "DIRECT3D12/luxmesh.vert",
        "DIRECT3D12/luxmesh.frag",
        "DIRECT3D12/luxscene.vert",
        "DIRECT3D12/luxscene.frag",
        "DIRECT3D12/luxshadow.vert",
        "DIRECT3D12/luxshadow.frag",
        "DIRECT3D12/luxfullscreen.vert",
        "DIRECT3D12/luxsky.frag",
        "DIRECT3D12/luxtone.frag"
    )
    foreach ($file in @(
        "shape-cube.fbx", "shape-cube-half.fbx", "shape-cube-rounded.fbx",
        "shape-cylinder.fbx", "shape-cylinder-detailed.fbx", "shape-cylinder-half.fbx",
        "shape-hexagon.fbx", "shape-slope.fbx", "shape-triangular-prism.fbx", "floor-square.fbx",
        "Textures/colormap.png", "License.txt", "Provenance.json"
    )) {
        "Resources/Models/Primitives/KenneyPrototype/$file"
    }
}

function Assert-LuxWindowsRuntime {
    param([Parameter(Mandatory = $true)][string]$Directory)

    foreach ($relativePath in (Get-LuxWindowsRuntimeFiles)) {
        $path = Join-Path $Directory $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "Packaging integrity failure: required runtime file missing or empty: $relativePath"
        }
    }
}

function Copy-LuxWindowsRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    Assert-LuxWindowsRuntime -Directory $Source
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    # Preserve transitive dependency DLLs and optional DXIL validation support as well.
    Get-ChildItem -LiteralPath $Source -Filter "*.dll" -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Force
    }
    foreach ($file in @("gpu.data", "gpu.cfg")) {
        Copy-Item -LiteralPath (Join-Path $Source $file) -Destination $Destination -Force
    }
    foreach ($directory in @("DIRECT3D12", "managed", "Resources")) {
        Copy-Item -LiteralPath (Join-Path $Source $directory) -Destination $Destination -Recurse -Force
    }
    Assert-LuxWindowsRuntime -Directory $Destination
}
