param(
    [Parameter(Mandatory = $false)]
    [string]$ArchivePath = ""
)

. (Join-Path $PSScriptRoot "Utf8.ps1")
$ErrorActionPreference = "Stop"
$dxcUrl = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip"
$expected = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"

$tempZip = $false
if (-not $ArchivePath -or -not (Test-Path $ArchivePath)) {
    $ArchivePath = Join-Path $env:TEMP "dxc_2026_09_29.zip"
    Write-Host "Downloading DXC from $dxcUrl..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        & curl.exe -sSL --fail --retry 5 --retry-connrefused --retry-delay 2 $dxcUrl -o $ArchivePath
        if ($LASTEXITCODE -ne 0) { throw "curl failed to download DXC archive" }
    } else {
        Invoke-WebRequest -Uri $dxcUrl -OutFile $ArchivePath
    }
    $tempZip = $true
}

$archive = (Resolve-Path -LiteralPath $ArchivePath -ErrorAction Stop).Path
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "DXC checksum mismatch" }
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $PSScriptRoot "dxc") -Force
if ($tempZip -and (Test-Path $archive)) { Remove-Item $archive -Force }
Write-Host "Portable DXC installed in tools/dxc"
