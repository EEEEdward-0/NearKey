$ErrorActionPreference = 'Stop'
$config = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\BluetoothUnlockDemo'
$result = [pscustomobject]@{
    ProbeResult = $config.LastProbeResult
    ProbeError = $config.LastProbeError
    ProbeAgeMs = $config.LastProbeAgeMs
    IdentityLocal = $config.LastIdentityLocal
    IdentityMicrosoftName = $config.LastIdentityMicrosoftName
}
$output = Join-Path $PSScriptRoot 'diagnostic-result.json'
$result | ConvertTo-Json -Compress | Set-Content -LiteralPath $output -Encoding UTF8
