# Run in an elevated 64-bit PowerShell to remove this demo.
$ErrorActionPreference = 'Stop'
$guid = '{c6830b85-4394-479b-9998-e919461b6081}'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请使用管理员权限运行卸载脚本。'
}
& (Join-Path $PSScriptRoot 'stop-monitor.ps1')
$providerKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$guid"
$classKey = "HKLM:\SOFTWARE\Classes\CLSID\$guid"
if (Test-Path -LiteralPath $providerKey) { Remove-Item -LiteralPath $providerKey -Force }
if (Test-Path -LiteralPath $classKey) { Remove-Item -LiteralPath $classKey -Recurse -Force }
$configKey = 'HKLM:\SOFTWARE\BluetoothUnlockDemo'
if (Test-Path -LiteralPath $configKey) { Remove-Item -LiteralPath $configKey -Force }
$stateKey = 'HKCU:\Software\BluetoothUnlockDemo'
if (Test-Path -LiteralPath $stateKey) { Remove-Item -LiteralPath $stateKey -Force }
$binary = Join-Path (Join-Path $env:ProgramFiles 'BluetoothUnlockDemo') 'BluetoothCredentialProvider.dll'
if (Test-Path -LiteralPath $binary) { Remove-Item -LiteralPath $binary -Force }
Write-Output 'demo 注册信息与本机保存的凭据已移除。'
