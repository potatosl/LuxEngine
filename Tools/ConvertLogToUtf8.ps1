param(
    [Parameter(Mandatory = $true)][string[]]$Path,
    [int]$LegacyCodePage = [System.Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage
)

. (Join-Path $PSScriptRoot "Utf8.ps1")
$ErrorActionPreference = "Stop"
foreach ($logPath in $Path) {
    ConvertTo-LuxUtf8Log -Path $logPath -LegacyCodePage $LegacyCodePage
    Write-Output "UTF-8: $logPath"
}
