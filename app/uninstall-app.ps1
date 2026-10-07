# Run in an elevated 64-bit PowerShell to remove the application and credentials.
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请使用管理员权限运行卸载脚本。'
}
$destination = Join-Path $env:ProgramFiles 'BluetoothUnlockDemo'
$service = Get-Service -Name BluetoothUnlockService -ErrorAction SilentlyContinue
if ($service) {
    Stop-Service -Name BluetoothUnlockService -ErrorAction Stop
    $service.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(60))
    & sc.exe delete BluetoothUnlockService
    if ($LASTEXITCODE -ne 0) { throw '服务删除失败。' }
}
$task = Get-ScheduledTask -TaskName BluetoothUnlockSession -ErrorAction SilentlyContinue
if ($task) {
    Stop-ScheduledTask -TaskName BluetoothUnlockSession -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName BluetoothUnlockSession -Confirm:$false
}
# Preserve the current shared settings for a later per-user reinstall.
$configKey = 'HKLM:\SOFTWARE\BluetoothUnlockApp'
$config = Get-ItemProperty -LiteralPath $configKey -ErrorAction SilentlyContinue
if ($config.UserSid -eq $identity.User.Value -and $config.DataDirectory) {
    $shared = Join-Path $config.DataDirectory 'settings.ini'
    if (Test-Path -LiteralPath $shared) {
        $local = Join-Path $env:LOCALAPPDATA 'BluetoothUnlock'
        New-Item -ItemType Directory -Path $local -Force | Out-Null
        Copy-Item -LiteralPath $shared -Destination (Join-Path $local 'settings.ini') -Force
    }
}
if (Test-Path -LiteralPath $configKey) {
    New-ItemProperty -LiteralPath $configKey -Name ServiceEnabled -Value 0 -PropertyType DWord -Force | Out-Null
}

Get-CimInstance Win32_Process -Filter "Name = 'BluetoothBackend.exe'" |
    Where-Object { $_.ExecutablePath -eq (Join-Path $destination 'BluetoothBackend.exe') } |
    ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force
        Wait-Process -Id $_.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
    }
Remove-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'BluetoothUnlock' -ErrorAction SilentlyContinue
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\近钥 NearKey.lnk'
if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut -Force }
$oldShortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\蓝牙靠近解锁.lnk'
if (Test-Path -LiteralPath $oldShortcut) { Remove-Item -LiteralPath $oldShortcut -Force }
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $root 'demo\uninstall-demo.ps1')
foreach ($name in @('BluetoothBackend.exe', 'BluetoothUnlock.UI.exe', 'BluetoothUnlock.UI.dll',
                    'BluetoothUnlock.UI.deps.json', 'BluetoothUnlock.UI.runtimeconfig.json')) {
    $path = Join-Path $destination $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}
Write-Output '应用和登录组件已卸载；配置与事件记录仍保留在 LocalAppData / ProgramData。'
