param([string]$Zig = 'zig')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
foreach ($item in @(
    @{ Target = 'x86_64-linux-gnu'; Folder = 'linux-x64' },
    @{ Target = 'aarch64-linux-gnu'; Folder = 'linux-arm64' }
)) {
    $output = Join-Path $root (Join-Path 'dist' $item.Folder)
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    & $Zig c++ -target $item.Target -std=c++17 -O2 -o (Join-Path $output 'nearkey') (Join-Path $PSScriptRoot 'nearkey.cpp')
    if ($LASTEXITCODE -ne 0) { throw "Linux 后台构建失败：$($item.Target)" }
    & $Zig cc -target $item.Target -O2 -shared -fPIC -o (Join-Path $output 'pam_nearkey.so') (Join-Path $PSScriptRoot 'pam_nearkey.c')
    if ($LASTEXITCODE -ne 0) { throw "Linux PAM 构建失败：$($item.Target)" }
}
