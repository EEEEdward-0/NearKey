# Update only the settings UI; leave the backend and credential provider running.
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script as administrator.'
}

$destination = Join-Path $env:ProgramFiles 'BluetoothUnlockDemo'
$platform = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'Arm64') { 'ARM64' } else { 'x64' }
$source = Join-Path $PSScriptRoot $(if ($platform -eq 'ARM64') { 'frontend\bin\publish-arm64' } else { 'frontend\bin\publish' })
foreach ($name in @('BluetoothUnlock.UI.exe', 'BluetoothUnlock.UI.dll',
                    'BluetoothUnlock.UI.deps.json', 'BluetoothUnlock.UI.runtimeconfig.json')) {
    $file = Join-Path $source $name
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing build file: $file" }
}
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothUnlock.UI.exe'" |
    Where-Object { $_.ExecutablePath -eq (Join-Path $destination 'BluetoothUnlock.UI.exe') } |
    ForEach-Object { Stop-Process -Id $_.ProcessId }
foreach ($name in @('BluetoothUnlock.UI.exe', 'BluetoothUnlock.UI.dll',
                    'BluetoothUnlock.UI.deps.json', 'BluetoothUnlock.UI.runtimeconfig.json')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $destination $name) -Force
}
Write-Output 'Settings UI updated.'
