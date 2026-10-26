; Inno Setup 脚本 - LiteVLC Player Windows 11 安装程序
; 编译：ISCC.exe /DMyAppVersion=1.1.0 installer.iss
; 产物：dist\installer\LiteVlcPlayer-<版本>-Setup.exe
; 打包前置：先跑 package.bat（生成 dist\LiteVlcPlayer 部署目录）
#ifndef MyAppVersion
#define MyAppVersion "1.1.5"
#endif
#define MyAppName "LiteVLC Player"
#define MyAppPublisher "LiteVLC Team"
#define MyAppExeName "LiteVlcPlayer.exe"

[Setup]
AppId={{5A8E5C47-F27E-4688-92A5-885E47452CD1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=dist\installer
OutputBaseFilename=LiteVlcPlayer-{#MyAppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
DisableProgramGroupPage=yes
PrivilegesRequired=admin
; 安装程序自身的图标与版本信息
SetupIconFile=resources\app.ico
VersionInfoVersion={#MyAppVersion}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Windows 安装程序
VersionInfoProductName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
; 简体中文语言包随仓库提供：不同版本的 Inno Setup 安装包未必附带
; Languages\ChineseSimplified.isl（CI 的 windows-2022 镜像就没有），
; 用 compiler: 前缀会直接编译失败，所以固定用仓库内这一份。
Name: "chinesesimp"; MessagesFile: "packaging\windows\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
; dist\LiteVlcPlayer 由 package.bat 用 windeployqt + LibVLC 组装（含 Qt 运行库、VLC 插件）
Source: "dist\LiteVlcPlayer\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 用户数据在 %APPDATA%\LiteVlcPlayer，卸载时一并清理（配置/进度/片源）
Type: filesandordirs; Name: "{userappdata}\LiteVlcPlayer"
