$ErrorActionPreference = 'Stop'
$binary = Join-Path $PSScriptRoot 'smoke\x64\Release\ProviderSmoke.exe'
& $binary --compare-password
$result = $LASTEXITCODE
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'password-comparison-result.txt') -Value $result -Encoding ascii
Write-Host 'Result saved. Press Enter to close this window.'
[void][Console]::ReadLine()
exit $result
