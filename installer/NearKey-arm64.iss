#define MyAppName "NearKey"
#define MyAppVersion "1.0.2"
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
OutputBaseFilename=NearKey-Setup-arm64-v{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
PrivilegesRequired=admin
RestartIfNeededByRun=no
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=NearKey - 近钥

[Files]
Source: "..\app\backend\ARM64\Release\BluetoothBackend.exe"; DestDir: "{app}\backend"; Flags: ignoreversion
Source: "..\app\frontend\bin\publish-arm64\BluetoothUnlock.UI.exe"; DestDir: "{app}\ui"; Flags: ignoreversion
Source: "..\app\frontend\bin\publish-arm64\BluetoothUnlock.UI.dll"; DestDir: "{app}\ui"; Flags: ignoreversion
Source: "..\app\frontend\bin\publish-arm64\BluetoothUnlock.UI.deps.json"; DestDir: "{app}\ui"; Flags: ignoreversion
Source: "..\app\frontend\bin\publish-arm64\BluetoothUnlock.UI.runtimeconfig.json"; DestDir: "{app}\ui"; Flags: ignoreversion
Source: "..\demo\credential-provider\ARM64\Release\SampleV2CredentialProvider.dll"; DestDir: "{app}\credential-provider"; Flags: ignoreversion
Source: "..\demo\setup\ARM64\Release\BluetoothSetup.exe"; DestDir: "{app}\setup"; Flags: ignoreversion
Source: "..\app\install-app.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "install-checked.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\app\uninstall-app.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\NearKey - 近钥"; Filename: "{app}\ui\{#MyAppExeName}"
Name: "{autodesktop}\NearKey - 近钥"; Filename: "{app}\ui\{#MyAppExeName}"

[Run]
Filename: "{app}\ui\{#MyAppExeName}"; Description: "启动 NearKey 设置"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\uninstall-app.ps1"""; RunOnceId: "NearKeyUninstall"

[Code]
procedure WriteInstallerLog(Message: String);
var
  DirectoryPath: String;
begin
  DirectoryPath := ExpandConstant('{commonappdata}\NearKey');
  if ForceDirectories(DirectoryPath) then
    SaveStringToFile(DirectoryPath + '\installer.log',
      GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #9 + Message + #13#10, True);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  WriteInstallerLog('password_setup_started');

  if not Exec(ExpandConstant('{app}\setup\BluetoothSetup.exe'), '', '',
    SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then begin
    WriteInstallerLog('password_setup_start_failed');
    RaiseException('无法启动 Windows 账户密码配置，NearKey 安装已停止。');
  end;
  if ResultCode <> 0 then begin
    WriteInstallerLog('password_setup_failed: exit=' + IntToStr(ResultCode));
    RaiseException('Windows 账户密码配置未完成，NearKey 安装已停止。');
  end;
  WriteInstallerLog('password_setup_succeeded');

  WriteInstallerLog('service_install_started');
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\install-checked.ps1') + '"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
    WriteInstallerLog('service_install_start_failed');
    RaiseException('无法启动后台服务安装，NearKey 安装已停止。');
  end;
  if ResultCode <> 0 then begin
    WriteInstallerLog('service_install_failed: exit=' + IntToStr(ResultCode));
    RaiseException('后台服务安装失败，NearKey 安装已停止。请查看 ProgramData\NearKey\install.log。');
  end;
  WriteInstallerLog('service_install_succeeded');
end;






