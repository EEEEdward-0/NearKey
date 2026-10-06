$ErrorActionPreference = 'Stop'
$binary = Join-Path $PSScriptRoot 'smoke\x64\Release\ProviderSmoke.exe'
$result = & $binary --verify-password
$result | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'password-verification-result.txt') -Encoding ascii
exit $LASTEXITCODE
