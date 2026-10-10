$ErrorActionPreference = 'Stop'
function Write-InstallLog([string]$message) {
    try {
        $directory = Join-Path $env:ProgramData 'NearKey'
        [IO.Directory]::CreateDirectory($directory) | Out-Null
        [IO.File]::AppendAllText((Join-Path $directory 'install.log'),
            "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`t$message`r`n", [Text.Encoding]::UTF8)
    } catch { }
}
Write-InstallLog 'service_install_started'
try {
    & (Join-Path $PSScriptRoot 'install-app.ps1')
    Write-InstallLog 'service_install_succeeded'
} catch {
    Write-InstallLog "service_install_failed: $($_.Exception.GetType().Name): $($_.Exception.Message)"
    # Inno Setup runs this step without a console; show failures instead of hiding them.
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show(
        "NearKey 安装未完成：$($_.Exception.Message)`n日志：$env:ProgramData\NearKey\install.log",
        'NearKey 安装失败',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    exit 1
}
