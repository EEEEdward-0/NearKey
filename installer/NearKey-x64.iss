#define MyAppName "NearKey"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "EEEEdward-0"
#define MyAppExeName "BluetoothUnlock.UI.exe"

[Setup]
AppId={{B7C4F1A8-6CE1-4D3F-9E32-100000000001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\NearKey
DefaultGroupName=NearKey - 近钥
OutputDir=..\release
OutputBaseFilename=NearKey-Setup-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
RestartIfNeededByRun=no
DisableRestartPrompt=yes
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=NearKey - 近钥

[Files]
Source: "..\app\backend\x64\Release\BluetoothBackend.exe"; DestDir: "{app}\backend"; Flags: ignoreversion
Source: "..\app\frontend\bin\publish\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\demo\credential-provider\x64\Release\SampleV2CredentialProvider.dll"; DestDir: "{app}\credential-provider"; Flags: ignoreversion
Source: "..\demo\setup\x64\Release\BluetoothSetup.exe"; DestDir: "{app}\setup"; Flags: ignoreversion
Source: "..\app\install-app.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\app\uninstall-app.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\NearKey - 近钥"; Filename: "{app}\ui\{#MyAppExeName}"
Name: "{autodesktop}\NearKey - 近钥"; Filename: "{app}\ui\{#MyAppExeName}"

[Run]
Filename: "{app}\setup\BluetoothSetup.exe"; Description: "先配置 Windows 账户密码"; StatusMsg: "正在打开密码配置..."; Flags: postinstall waituntilterminated
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\install-app.ps1"""; StatusMsg: "正在安装 NearKey 后台服务..."; Flags: runhidden waituntilterminated
Filename: "{app}\ui\{#MyAppExeName}"; Description: "启动 NearKey 设置"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\uninstall-app.ps1"""; RunOnceId: "NearKeyUninstall"



