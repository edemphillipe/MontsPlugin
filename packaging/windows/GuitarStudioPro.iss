; Windows installer for Guitar Studio Pro (Inno Setup 6).
; Built by CI:  iscc /DAppVersion=0.4.0 /DSourceExe=path\to\exe /DOutputDir=dist packaging\windows\GuitarStudioPro.iss
; When the WINDOWS_CERT_* secrets exist, CI signs the program and this installer (see docs/ASSINATURA.md).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\..\build\src\app\GuitarStudioPro_artefacts\Release\Guitar Studio Pro.exe"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6C1E3A4B-8F2D-4E51-9A7C-3B5D2F1E8A90}
AppName=Guitar Studio Pro
AppVersion={#AppVersion}
AppVerName=Guitar Studio Pro {#AppVersion}
AppPublisher=MontsPlugin
DefaultDirName={autopf}\Guitar Studio Pro
DefaultGroupName=Guitar Studio Pro
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Guitar Studio Pro.exe
SetupIconFile=..\..\resources\icon.ico
OutputDir={#OutputDir}
OutputBaseFilename=GuitarStudioPro-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
#ifdef Sign
SignTool=signtool
SignedUninstaller=yes
#endif

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\docs\INSTALACAO.md"; DestDir: "{app}"; DestName: "LEIA-ME.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Guitar Studio Pro"; Filename: "{app}\Guitar Studio Pro.exe"
Name: "{autodesktop}\Guitar Studio Pro"; Filename: "{app}\Guitar Studio Pro.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Guitar Studio Pro.exe"; Description: "{cm:LaunchProgram,Guitar Studio Pro}"; Flags: nowait postinstall skipifsilent
