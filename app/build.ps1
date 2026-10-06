$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw '找不到 Visual Studio 2022 C++ Build Tools。' }
& $msbuild (Join-Path $root 'demo\credential-provider\SampleV2CredentialProvider.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw '登录组件构建失败。' }
& $msbuild (Join-Path $PSScriptRoot 'backend\BluetoothBackend.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw '后台构建失败。' }
& dotnet publish (Join-Path $PSScriptRoot 'frontend\BluetoothUnlock.UI.csproj') -c Release --self-contained false -o (Join-Path $PSScriptRoot 'frontend\bin\publish') --nologo
if ($LASTEXITCODE -ne 0) { throw '设置界面构建失败。' }
Write-Output '构建完成。'
