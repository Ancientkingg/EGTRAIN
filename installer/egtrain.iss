; EGTRAIN installer for Windows. It installs for the current user and asks for no administrator rights.
; Compile with Inno Setup 6.3 or newer:
;   ISCC.exe /DVersion=X.Y.Z /DPayload=<package directory> [/O<output directory>] installer\egtrain.iss
; docs\development\windows-installer.md describes the parameters and what the installer does.

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
UninstallDisplayName=EGTRAIN
AppPublisher=EGTRAIN
AppPublisherURL=https://github.com/Ancientkingg/EGTRAIN
DefaultDirName={userpf}\EGTRAIN
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
ChangesAssociations=yes
UninstallDisplayIcon={app}\QEGTRAIN.exe
SetupIconFile=..\EGTRAIN\QEGTRAIN\resources\app\egtrain.ico
LicenseFile=..\LICENSE
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
; An installation of version 1.0.3 or older holds the files of Qt 5. These are the ones that no file of the package replaces.
Type: files; Name: "{app}\Qt5*.dll"
Type: filesandordirs; Name: "{app}\bearer"
Type: files; Name: "{app}\styles\qwindowsvistastyle.dll"
Type: files; Name: "{app}\imageformats\qicns.dll"
Type: files; Name: "{app}\imageformats\qtga.dll"
Type: files; Name: "{app}\imageformats\qtiff.dll"
Type: files; Name: "{app}\imageformats\qwbmp.dll"
Type: files; Name: "{app}\imageformats\qwebp.dll"

[Icons]
Name: "{userprograms}\EGTRAIN"; Filename: "{app}\QEGTRAIN.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\EGTRAIN"; Filename: "{app}\QEGTRAIN.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; The .egscene file type belongs to the installed copy: it is for the current user, it opens a file with QEGTRAIN.exe of {app},
; and the uninstaller removes it. The portable archive registers nothing.
Root: HKCU; Subkey: "Software\Classes\.egscene"; ValueType: string; ValueName: ""; ValueData: "EGTRAIN.Scene"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\EGTRAIN.Scene"; ValueType: string; ValueName: ""; ValueData: "EGTRAIN case study"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\EGTRAIN.Scene\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\QEGTRAIN.exe,0"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\EGTRAIN.Scene\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\QEGTRAIN.exe"" ""%1"""; Flags: uninsdeletekey
