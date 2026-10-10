$ErrorActionPreference = 'Stop'
try {
    & (Join-Path $PSScriptRoot 'install-app.ps1')
} catch {
    # Inno Setup runs this step without a console; show failures instead of hiding them.
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show(
        "NearKey 安装未完成：$($_.Exception.Message)`n请检查密码配置，再重试安装。",
        'NearKey 安装失败',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    exit 1
}
