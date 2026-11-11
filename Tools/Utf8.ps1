$utf8Encoding = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8Encoding
[Console]::OutputEncoding = $utf8Encoding
$global:OutputEncoding = $utf8Encoding
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:DOTNET_CLI_FORCE_UTF8_ENCODING = 'true'
$env:LC_ALL = 'C'
$env:LC_TIME = 'C'

function ConvertTo-LuxUtf8Log {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$LegacyCodePage = [System.Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage
    )
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $bytes = [System.IO.File]::ReadAllBytes($resolvedPath)
    $strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) {
        $text = [System.Text.Encoding]::Unicode.GetString($bytes, 2, $bytes.Length - 2)
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255) {
        $text = [System.Text.Encoding]::BigEndianUnicode.GetString($bytes, 2, $bytes.Length - 2)
    } else {
        $latin1 = [System.Text.Encoding]::GetEncoding(28591)
        $legacy = [System.Text.Encoding]::GetEncoding(
            $LegacyCodePage, [System.Text.EncoderFallback]::ExceptionFallback,
            [System.Text.DecoderFallback]::ExceptionFallback)
        $lines = foreach ($line in $latin1.GetString($bytes).Split("`n")) {
            $lineBytes = $latin1.GetBytes($line)
            try { $strictUtf8.GetString($lineBytes) }
            catch [System.Text.DecoderFallbackException] { $legacy.GetString($lineBytes) }
        }
        $text = [string]::Join("`n", [string[]]$lines)
    }
    [System.IO.File]::WriteAllText($resolvedPath, $text, [System.Text.UTF8Encoding]::new($false))
}
