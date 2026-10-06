$binary = Join-Path $PSScriptRoot 'monitor\x64\Release\BluetoothMonitor.exe'
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothMonitor.exe'" |
    Where-Object { $_.ExecutablePath -eq $binary } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Write-Output '已停止此 demo 的蓝牙监听程序。'
