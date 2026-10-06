param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{12}$')]
    [string]$IPhoneAddress,
    [ValidateRange(-100, -20)]
    [int]$Threshold = -65
)
$ErrorActionPreference = 'Stop'
$binary = Join-Path $PSScriptRoot 'monitor\x64\Release\BluetoothMonitor.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw '找不到已编译的蓝牙监听程序。' }
$running = Get-CimInstance Win32_Process -Filter "Name = 'BluetoothMonitor.exe'" |
    Where-Object { $_.ExecutablePath -eq $binary }
if ($running) { throw '蓝牙监听程序已在运行。' }
$process = Start-Process -FilePath $binary -ArgumentList @($IPhoneAddress, $Threshold) -WindowStyle Hidden -PassThru
Write-Output "蓝牙监听程序已启动，PID=$($process.Id)。"
