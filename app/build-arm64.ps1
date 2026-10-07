$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw '找不到 Visual Studio 2022 C++ Build Tools。' }
foreach ($project in @(
    (Join-Path $root 'demo\credential-provider\SampleV2CredentialProvider.vcxproj'),
    (Join-Path $root 'demo\setup\BluetoothSetup.vcxproj'),
    (Join-Path $PSScriptRoot 'backend\BluetoothBackend.vcxproj')
)) {
    & $msbuild $project /p:Configuration=Release /p:Platform=ARM64 /m /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "ARM64 构建失败：$project" }
}
& dotnet publish (Join-Path $PSScriptRoot 'frontend\BluetoothUnlock.UI.csproj') -c Release -r win-arm64 --self-contained false -o (Join-Path $PSScriptRoot 'frontend\bin\publish-arm64') --nologo
if ($LASTEXITCODE -ne 0) { throw 'ARM64 设置界面构建失败。' }
Write-Output 'Windows ARM64 构建完成。'
