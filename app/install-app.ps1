# Run in an elevated 64-bit PowerShell after app/build.ps1 succeeds.
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请使用管理员权限运行安装脚本。'
}
$root = Split-Path $PSScriptRoot -Parent
$backend = Join-Path $PSScriptRoot 'backend\x64\Release\BluetoothBackend.exe'
$uiDirectory = Join-Path $PSScriptRoot 'frontend\bin\publish'
$destination = Join-Path $env:ProgramFiles 'BluetoothUnlockDemo'
if (-not (Test-Path -LiteralPath $backend) -or
    -not (Test-Path -LiteralPath (Join-Path $uiDirectory 'BluetoothUnlock.UI.exe'))) {
    throw '请先运行 app\build.ps1。'
}
& (Join-Path $root 'demo\install-demo.ps1')
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$installedBackend = Join-Path $destination 'BluetoothBackend.exe'
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothBackend.exe'" |
    Where-Object { $_.ExecutablePath -eq $installedBackend } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Copy-Item -LiteralPath $backend -Destination $installedBackend -Force
foreach ($name in @('BluetoothUnlock.UI.exe', 'BluetoothUnlock.UI.dll',
                    'BluetoothUnlock.UI.deps.json', 'BluetoothUnlock.UI.runtimeconfig.json')) {
    Copy-Item -LiteralPath (Join-Path $uiDirectory $name) -Destination (Join-Path $destination $name) -Force
}
$shortcutPath = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\蓝牙靠近解锁.lnk'
$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath)
$shortcut.TargetPath = Join-Path $destination 'BluetoothUnlock.UI.exe'
$shortcut.WorkingDirectory = $destination
$shortcut.Description = '蓝牙靠近解锁设置'
$shortcut.Save()
Write-Output '应用已安装到 Program Files，开始菜单快捷方式已创建。'
