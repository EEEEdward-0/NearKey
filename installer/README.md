# NearKey 安装器

`NearKey-x64.iss` 和 `NearKey-arm64.iss` 是 Windows 一键安装包的 Inno Setup 配置。它会把后台服务、登录组件、设置窗口和卸载脚本放到同一安装目录，并创建开始菜单和桌面快捷方式。

构建前先完成 Windows x64 构建，再使用 Inno Setup 编译：

```powershell
& .\app\build.ps1
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" .\installer\NearKey-x64.iss
```

ARM64 版本把脚本文件名换成 `NearKey-arm64.iss`。

输出文件按版本号命名，例如：

```text
release/NearKey-Setup-x64-v1.0.1.exe
```

