# Run in an elevated 64-bit PowerShell to remove the application and credentials.
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请使用管理员权限运行卸载脚本。'
}
$destination = Join-Path $env:ProgramFiles 'BluetoothUnlockDemo'
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothBackend.exe'" |
    Where-Object { $_.ExecutablePath -eq (Join-Path $destination 'BluetoothBackend.exe') } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Remove-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'BluetoothUnlock' -ErrorAction SilentlyContinue
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\蓝牙靠近解锁.lnk'
if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut -Force }
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $root 'demo\uninstall-demo.ps1')
foreach ($name in @('BluetoothBackend.exe', 'BluetoothUnlock.UI.exe', 'BluetoothUnlock.UI.dll',
                    'BluetoothUnlock.UI.deps.json', 'BluetoothUnlock.UI.runtimeconfig.json')) {
    $path = Join-Path $destination $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}
Write-Output '应用和登录组件已卸载；用户设置与事件记录仍保留在 LocalAppData。'
