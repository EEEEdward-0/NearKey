# Run in an elevated 64-bit PowerShell after BluetoothSetup.exe succeeds.
$ErrorActionPreference = 'Stop'
$guid = '{c6830b85-4394-479b-9998-e919461b6081}'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请使用管理员权限运行安装脚本。'
}
$config = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\BluetoothUnlockDemo' -ErrorAction Stop
if ($config.UserSid -ne $identity.User.Value -or -not $config.Password) {
    throw '请先用当前 Windows 账户运行 BluetoothSetup.exe，完成本机密码配置。'
}
$source = Join-Path $PSScriptRoot 'credential-provider\x64\Release\SampleV2CredentialProvider.dll'
$destinationDirectory = Join-Path $env:ProgramFiles 'BluetoothUnlockDemo'
$destination = Join-Path $destinationDirectory 'BluetoothCredentialProvider.dll'
if (-not (Test-Path -LiteralPath $source)) { throw '找不到已编译的登录组件。' }
New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $destination -Force

# Register only our own provider. Windows' built-in PIN and password tiles remain available.
$classKey = "HKLM:\SOFTWARE\Classes\CLSID\$guid\InprocServer32"
$providerKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$guid"
New-Item -Path $classKey -Force | Out-Null
Set-Item -LiteralPath $classKey -Value $destination
New-ItemProperty -LiteralPath $classKey -Name ThreadingModel -Value 'Apartment' -PropertyType String -Force | Out-Null
New-Item -Path $providerKey -Force | Out-Null
Set-Item -LiteralPath $providerKey -Value '靠近解锁'
Write-Output '蓝牙登录组件已注册。Windows 原有登录方式仍可使用。'
