$ErrorActionPreference = 'Stop'
$config = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\BluetoothUnlockDemo'
$result = [pscustomobject]@{
    ProbeResult = $config.LastProbeResult
    ProbeError = $config.LastProbeError
    ProbeAgeMs = $config.LastProbeAgeMs
    IdentityLocal = $config.LastIdentityLocal
    IdentityMicrosoftName = $config.LastIdentityMicrosoftName
    AutoDefault = $config.LastAutoDefault
    AutoSelected = $config.LastAutoSelected
    AutoScenario = $config.LastAutoScenario
    ExistingSession = $config.LastExistingSession
    SessionScanCount = $config.LastSessionScanCount
    ConsoleSession = $config.LastConsoleSession
    SerializationTick = $config.LastSerializationTick
    SerializationResponse = $config.LastSerializationResponse
    SerializationHr = $config.LastSerializationHr
    AuthStatus = $config.LastAuthStatus
    AuthSubstatus = $config.LastAuthSubstatus
    AutoRefreshTick = $config.LastAutoRefreshTick
}
$output = Join-Path $PSScriptRoot 'diagnostic-result.json'
$result | ConvertTo-Json -Compress | Set-Content -LiteralPath $output -Encoding UTF8
