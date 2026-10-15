; EGTRAIN installer for Windows. It installs for the current user and asks for no administrator rights.
; Compile with Inno Setup 6.3 or newer:
;   ISCC.exe /DVersion=X.Y.Z /DPayload=<package directory> [/O<output directory>] installer\egtrain.iss
; installer\README.md describes the parameters and what the installer does.

#ifndef Version
#error Pass the release version with /DVersion=X.Y.Z.
#endif
#ifndef Payload
#error Pass the assembled Windows package directory with /DPayload=<directory>.
#endif

[Setup]
; The AppId names the uninstall entry and ties an upgrade to the installation it replaces. Never change it.
AppId={{16796959-230D-4A37-879B-D3963171F664}
AppName=EGTRAIN
AppVersion={#Version}
AppPublisher=TU Delft
AppPublisherURL=https://github.com/Ancientkingg/EGTRAIN
DefaultDirName={userpf}\EGTRAIN
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
UninstallDisplayIcon={app}\QEGTRAIN.exe
SetupIconFile=..\EGTRAIN\QEGTRAIN\resources\app\egtrain.ico
OutputBaseFilename=EGTRAIN-Setup-{#Version}-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "installed-by-setup.ini"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
Type: filesandordirs; Name: "{app}\Scenes"

[Icons]
Name: "{userprograms}\EGTRAIN"; Filename: "{app}\QEGTRAIN.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\EGTRAIN"; Filename: "{app}\QEGTRAIN.exe"; WorkingDir: "{app}"; Tasks: desktopicon
