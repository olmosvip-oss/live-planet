; Loopwright for Windows: one installer that puts the VST3 and CLAP plugins where every DAW looks,
; installs the app with a Start menu shortcut, and can be removed from Settings > Apps.
; Built in CI with Inno Setup: iscc /DAppVersion=0.9.x /DSrc=<folder with the built files> loopwright.iss
#ifndef AppVersion
  #define AppVersion "0.9.0"
#endif
#ifndef Src
  #define Src "..\..\dist"
#endif

[Setup]
AppId={{6E1B7C52-4A0E-4C1E-9C55-2B1F3B6E9A10}
AppName=Loopwright
AppVersion={#AppVersion}
AppPublisher=Loopwright
AppPublisherURL=https://loop-wright.com
AppSupportURL=https://loop-wright.com/support
DefaultDirName={autopf}\Loopwright
DefaultGroupName=Loopwright
DisableProgramGroupPage=yes
OutputBaseFilename=Loopwright-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Loopwright
UninstallDisplayIcon={app}\Loopwright.exe
CloseApplications=yes

[Types]
Name: "full"; Description: "Everything (recommended)"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "app";  Description: "Loopwright app";  Types: full custom
Name: "vst3"; Description: "VST3 plugin (Ableton, FL Studio, Cubase, Studio One, Reaper...)"; Types: full custom
Name: "clap"; Description: "CLAP plugin (Bitwig, Reaper, FL Studio 2024+)"; Types: full custom

[Files]
Source: "{#Src}\Loopwright.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Src}\Loopwright.vst3\*"; DestDir: "{commoncf64}\VST3\Loopwright.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Src}\Loopwright.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion

[Icons]
Name: "{group}\Loopwright"; Filename: "{app}\Loopwright.exe"; Components: app
Name: "{group}\Uninstall Loopwright"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Loopwright"; Filename: "{app}\Loopwright.exe"; Components: app; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Put a Loopwright icon on the desktop"; Components: app

[Run]
Filename: "{app}\Loopwright.exe"; Description: "Open Loopwright now"; Components: app; Flags: nowait postinstall skipifsilent
