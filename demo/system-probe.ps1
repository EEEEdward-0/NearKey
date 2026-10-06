$ErrorActionPreference = 'Stop'
$name = 'BluetoothUnlockDemo-SystemProbe-' + [guid]::NewGuid().ToString('N')
$exe = Join-Path $PSScriptRoot 'smoke\x64\Release\ProviderSmoke.exe'
$dll = Join-Path $env:ProgramFiles 'BluetoothUnlockDemo\BluetoothCredentialProvider.dll'
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$action = New-ScheduledTaskAction -Execute $exe -Argument ('"' + $dll + '" ' + $sid)
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
try {
    Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Force | Out-Null
    Start-ScheduledTask -TaskName $name
    Start-Sleep -Seconds 5
    $result = (Get-ScheduledTaskInfo -TaskName $name).LastTaskResult
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'system-probe-result.txt') -Value $result -Encoding ascii
} finally {
    Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
}
