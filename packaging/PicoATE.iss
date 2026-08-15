#ifndef MyAppVersion
  #define MyAppVersion "0.2.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\out\build\vs2022-qt6-all\portable\Release\PicoATE.UI"
#endif
#ifndef OutputDir
  #define OutputDir "..\out\installer"
#endif

#define MyAppName "PicoATE"
#define MyAppExeName "PicoATE.UI.exe"

[Setup]
AppId={{A80D453B-579F-47BB-BDD1-456793B8D2CA}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=PicoATE
DefaultDirName={localappdata}\Programs\PicoATE
DisableDirPage=no
UsePreviousAppDir=yes
DefaultGroupName=PicoATE
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=PicoATE-Setup-{#MyAppVersion}-x64
SetupIconFile=..\ui\src\assets\PicoATE.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
AppMutex=PicoATE.UI
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式"; Flags: checkedonce

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "ProductRouting.json,projects\*,log\*,diagnostics\*"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\ProductRouting.json"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Dirs]
Name: "{app}\log"; Flags: uninsneveruninstall
Name: "{app}\diagnostics"; Flags: uninsneveruninstall
Name: "{app}\projects"; Flags: uninsneveruninstall

[Icons]
Name: "{group}\PicoATE"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\PicoATE"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 PicoATE"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
