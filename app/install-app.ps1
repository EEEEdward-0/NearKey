# Run in an elevated 64-bit PowerShell after app/build.ps1 succeeds.
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请使用管理员权限运行安装脚本。'
}
function Copy-AppFile([string]$sourcePath, [string]$destinationPath) {
    # Windows may retain an executable mapping briefly after process termination.
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try { Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force; return }
        catch {
            if ($attempt -eq 9) { throw }
            Start-Sleep -Milliseconds 500
        }
    }
}
$root = Split-Path $PSScriptRoot -Parent
$packaged = Test-Path (Join-Path $PSScriptRoot 'backend')
$platform = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString() -eq 'Arm64') { 'ARM64' } else { 'x64' }
$backend = if ($packaged) { Join-Path $PSScriptRoot 'backend\BluetoothBackend.exe' } else { Join-Path $PSScriptRoot "backend\$platform\Release\BluetoothBackend.exe" }
$uiDirectory = if ($packaged) { Join-Path $PSScriptRoot 'ui' } else { Join-Path $PSScriptRoot $(if ($platform -eq 'ARM64') { 'frontend\bin\publish-arm64' } else { 'frontend\bin\publish' }) }
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
if ($packaged) {
    $guid = '{c6830b85-4394-479b-9998-e919461b6081}'
    $source = Join-Path $PSScriptRoot 'credential-provider\SampleV2CredentialProvider.dll'
    $providerDestination = Join-Path $destination 'BluetoothCredentialProvider.dll'
    if (-not (Test-Path -LiteralPath $source)) { throw '找不到已打包的 Windows 登录组件。' }
    Copy-AppFile $source $providerDestination
    $classKey = "HKLM:\SOFTWARE\Classes\CLSID\$guid\InprocServer32"
    $providerKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$guid"
    New-Item -Path $classKey -Force | Out-Null
    Set-Item -LiteralPath $classKey -Value $providerDestination
    New-ItemProperty -LiteralPath $classKey -Name ThreadingModel -Value 'Apartment' -PropertyType String -Force | Out-Null
    New-Item -Path $providerKey -Force | Out-Null
    Set-Item -LiteralPath $providerKey -Value '近钥 NearKey'
} else {
    & (Join-Path $root 'demo\install-demo.ps1')
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$installedBackend = Join-Path $destination 'BluetoothBackend.exe'
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothBackend.exe'" |
    Where-Object { $_.ExecutablePath -eq $installedBackend } |
    ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force
        Wait-Process -Id $_.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
    }
Copy-AppFile $backend $installedBackend
Get-CimInstance Win32_Process -Filter "Name = 'BluetoothUnlock.UI.exe'" |
    Where-Object { $_.ExecutablePath -eq (Join-Path $destination 'BluetoothUnlock.UI.exe') } |
    ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force
        Wait-Process -Id $_.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
    }
foreach ($name in @('BluetoothUnlock.UI.exe', 'BluetoothUnlock.UI.dll',
                    'BluetoothUnlock.UI.deps.json', 'BluetoothUnlock.UI.runtimeconfig.json')) {
    Copy-AppFile (Join-Path $uiDirectory $name) (Join-Path $destination $name)
}
$shortcutPath = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\近钥 NearKey.lnk'
$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath)
$shortcut.TargetPath = Join-Path $destination 'BluetoothUnlock.UI.exe'
$shortcut.WorkingDirectory = $destination
$shortcut.Description = '近钥 NearKey 设置'
$shortcut.Save()
$oldShortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\蓝牙靠近解锁.lnk'
Remove-Item -LiteralPath $oldShortcut -ErrorAction SilentlyContinue
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
    if (Test-Path -LiteralPath $legacySettings) {
        Copy-Item -LiteralPath $legacySettings -Destination $sharedSettings
    }
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
        New-Service -Name $serviceName -BinaryPathName $binaryPath -StartupType Automatic -DisplayName 'NearKey proximity service' | Out-Null
    }
    Set-Service -Name $serviceName -DisplayName 'NearKey proximity service'
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
