#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef RepoRoot
  #define RepoRoot ".."
#endif
#ifndef SourceDir
  #define SourceDir "..\artifacts\publish\ElkaVoiceMeeterFxHost\win-x64"
#endif
#ifndef OutputDir
  #define OutputDir "..\artifacts\release"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "ElkaVoiceMeeterFxHostSetup"
#endif

[Setup]
AppId={{35D7677D-46D7-46E7-A5B4-C54D3E442F55}
AppName=Elka VoiceMeeter FX Host
AppVersion={#AppVersion}
AppPublisher=ElkaSoft
AppPublisherURL=https://github.com/torment78/Elka.VoiceMeeterFxHost
AppSupportURL=https://github.com/torment78/Elka.VoiceMeeterFxHost/issues
AppUpdatesURL=https://github.com/torment78/Elka.VoiceMeeterFxHost/releases
DefaultDirName={autopf}\ElkaSoft\VoiceMeeter FX Host
DefaultGroupName=ElkaSoft\Elka VoiceMeeter FX Host
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#RepoRoot}\src\app-wpf\Assets\VoicemeeterDelay.ico
UninstallDisplayIcon={app}\Elka.VoiceMeeterFxHost.App.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern dark
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "disablepowerthrottling"; Description: "Disable Windows power throttling for Elka VoiceMeeter FX Host"; GroupDescription: "Performance options:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ElkaSoft\Elka VoiceMeeter FX Host"; Filename: "{app}\Elka.VoiceMeeterFxHost.App.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Elka VoiceMeeter FX Host"; Filename: "{app}\Elka.VoiceMeeterFxHost.App.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{sys}\powercfg.exe"; Parameters: "/powerthrottling disable /path ""{app}\Elka.VoiceMeeterFxHost.App.exe"""; StatusMsg: "Disabling Windows power throttling for Elka VoiceMeeter FX Host..."; Flags: runhidden waituntilterminated; Tasks: disablepowerthrottling
Filename: "{app}\Elka.VoiceMeeterFxHost.App.exe"; Description: "{cm:LaunchProgram,Elka VoiceMeeter FX Host}"; Flags: nowait postinstall skipifsilent
