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
$serviceName = 'BluetoothUnlockService'
$existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($existingService) {
    Stop-Service -Name $serviceName -ErrorAction Stop
    $existingService.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(60))
}
$configKey = 'HKLM:\SOFTWARE\BluetoothUnlockDemo'
$config = Get-ItemProperty -LiteralPath $configKey -ErrorAction Stop
if ($config.UserSid -ne $identity.User.Value -or -not $config.Password) {
    throw '请先用当前 Windows 账户完成本机密码配置。服务只绑定此账户。'
}
$serviceKey = 'HKLM:\SOFTWARE\BluetoothUnlockApp'
if (-not (Test-Path -LiteralPath $serviceKey)) { New-Item -Path $serviceKey | Out-Null }
$registryAcl = [Security.AccessControl.RegistrySecurity]::new()
$registryAcl.SetAccessRuleProtection($true, $false)
foreach ($entry in @(@('S-1-5-18', 'FullControl'), @('S-1-5-32-544', 'FullControl'), @('S-1-5-11', 'ReadKey'))) {
    $rule = [Security.AccessControl.RegistryAccessRule]::new(
        [Security.Principal.SecurityIdentifier]::new($entry[0]),
        [Security.AccessControl.RegistryRights]$entry[1],
        [Security.AccessControl.AccessControlType]::Allow)
    $registryAcl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $serviceKey -AclObject $registryAcl
New-ItemProperty -LiteralPath $serviceKey -Name UserSid -Value $identity.User.Value -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $serviceKey -Name ServiceEnabled -Value 0 -PropertyType DWord -Force | Out-Null
if (Get-ScheduledTask -TaskName BluetoothUnlockSession -ErrorAction SilentlyContinue) {
    Disable-ScheduledTask -TaskName BluetoothUnlockSession | Out-Null
    Stop-ScheduledTask -TaskName BluetoothUnlockSession -ErrorAction SilentlyContinue
}
& (Join-Path $root 'demo\install-demo.ps1')
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$installedBackend = Join-Path $destination 'BluetoothBackend.exe'
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothBackend.exe'" |
    Where-Object { $_.ExecutablePath -eq $installedBackend } |
    ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force
        Wait-Process -Id $_.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
    }
Copy-Item -LiteralPath $backend -Destination $installedBackend -Force
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothUnlock.UI.exe'" |
    Where-Object { $_.ExecutablePath -eq (Join-Path $destination 'BluetoothUnlock.UI.exe') } |
    ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force
        Wait-Process -Id $_.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
    }
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
# Shared data is writable only by the bound account, administrators and SYSTEM.
$dataRoot = Join-Path $env:ProgramData 'BluetoothUnlock'
$dataDirectory = Join-Path $dataRoot $identity.User.Value
$runtimeDirectory = Join-Path $dataRoot ($identity.User.Value + '-runtime')
New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $runtimeDirectory -Force | Out-Null
foreach ($folder in @($dataRoot, $dataDirectory, $runtimeDirectory)) {
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($entry in @(@('S-1-5-18', 'FullControl'), @('S-1-5-32-544', 'FullControl'),
                        @($identity.User.Value, $(if ($folder -eq $dataDirectory) { 'Modify' } else { 'ReadAndExecute' })))) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($entry[0]),
            [Security.AccessControl.FileSystemRights]$entry[1],
            [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit',
            [Security.AccessControl.PropagationFlags]::None,
            [Security.AccessControl.AccessControlType]::Allow)
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $folder -AclObject $acl
}
$sharedSettings = Join-Path $dataDirectory 'settings.ini'
$legacySettings = Join-Path $env:LOCALAPPDATA 'BluetoothUnlock\settings.ini'
if (-not (Test-Path -LiteralPath $sharedSettings)) {
    if (-not (Test-Path -LiteralPath $legacySettings)) { throw '请先在设置界面保存设备与解锁规则，再安装开机服务。' }
    Copy-Item -LiteralPath $legacySettings -Destination $sharedSettings
}
New-ItemProperty -LiteralPath $serviceKey -Name DataDirectory -Value $dataDirectory -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $serviceKey -Name RuntimeDirectory -Value $runtimeDirectory -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $serviceKey -Name ServiceExecutable -Value $installedBackend -PropertyType String -Force | Out-Null
try {
    $binaryPath = '"' + $installedBackend + '" --service'
    if ($existingService) {
        $changed = Invoke-CimMethod -Query "SELECT * FROM Win32_Service WHERE Name='BluetoothUnlockService'" -MethodName Change -Arguments @{ PathName = $binaryPath; StartMode = 'Automatic'; StartName = 'LocalSystem' }
        if ($changed.ReturnValue -ne 0) { throw "服务更新失败：$($changed.ReturnValue)" }
    } else {
        New-Service -Name $serviceName -BinaryPathName $binaryPath -StartupType Automatic -DisplayName 'Bluetooth proximity login' | Out-Null
    }
    & sc.exe failure $serviceName reset= 86400 actions= restart/10000/restart/30000/restart/60000
    if ($LASTEXITCODE -ne 0) { throw '服务恢复策略配置失败。' }
    & sc.exe failureflag $serviceName 1
    if ($LASTEXITCODE -ne 0) { throw '服务恢复标记配置失败。' }
    $action = New-ScheduledTaskAction -Execute $installedBackend -Argument '--session'
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $identity.Name
    $taskPrincipal = New-ScheduledTaskPrincipal -UserId $identity.Name -LogonType Interactive -RunLevel Limited
    $taskSettings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -RestartCount 3 -RestartInterval ([TimeSpan]::FromMinutes(1)) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    Register-ScheduledTask -TaskName 'BluetoothUnlockSession' -Action $action -Trigger $trigger -Principal $taskPrincipal -Settings $taskSettings -Force | Out-Null
    New-ItemProperty -LiteralPath $serviceKey -Name ServiceEnabled -Value 1 -PropertyType DWord -Force | Out-Null
    Start-Service -Name $serviceName
    Start-ScheduledTask -TaskName 'BluetoothUnlockSession'
    Remove-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name BluetoothUnlock -ErrorAction SilentlyContinue
} catch {
    New-ItemProperty -LiteralPath $serviceKey -Name ServiceEnabled -Value 0 -PropertyType DWord -Force | Out-Null
    Stop-Service -Name $serviceName -ErrorAction SilentlyContinue
    throw
}
Write-Output "开机服务已安装并启动，配置目录：$dataDirectory。首次登录和后续锁屏均需设备条件满足后按确认键；请先验证服务状态，再重启测试。"
