param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath
)

$ErrorActionPreference = "Stop"
$archive = (Resolve-Path -LiteralPath $ArchivePath -ErrorAction Stop).Path
$expected = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "DXC checksum mismatch" }
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $PSScriptRoot "dxc") -Force
Write-Host "Portable DXC installed in Tools/dxc"
