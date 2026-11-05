$ErrorActionPreference = "Stop"
$rootDir = (Get-Item $PSScriptRoot).Parent.FullName
$archive = Join-Path $rootDir "build/dxc_2026_09_29.zip"
New-Item -ItemType Directory -Force (Split-Path $archive) | Out-Null
Invoke-WebRequest "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip" -OutFile $archive
$expected = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "DXC checksum mismatch" }
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $PSScriptRoot "dxc") -Force
Write-Host "Portable DXC installed in Tools/dxc"
