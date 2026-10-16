# Windows installer

`egtrain.iss` is the Inno Setup script of the EGTRAIN installer for Windows. Compiled, it gives a Setup program that installs the assembled Windows package for the current user and asks for no administrator rights.

The program is named `EGTRAIN-Setup-<version>-x64.exe`. The name is fixed by `kAssetPatterns` in `EGTRAIN/QEGTRAIN/update/ReleaseInfo.cpp`, which accepts it for the key `windows-x64-installer`.

## Build

Compile on Windows with Inno Setup 6.3 or newer (`ISCC.exe`). Version 6.3 is the first one that accepts the architecture name `x64compatible`.

```text
ISCC.exe /DVersion=1.2.3 /DPayload=C:\build\dist\QEGTRAIN /OC:\build\installer-output installer\egtrain.iss
```

| Parameter | Required | Meaning |
| --- | --- | --- |
| `/DVersion=X.Y.Z` | yes | The release version, in the form that `tools/release/version.py` accepts. It becomes the version that Windows shows for the installed program and a part of the file name. |
| `/DPayload=<directory>` | yes | The assembled Windows package, with `QEGTRAIN.exe` at its top level: the directory that the Windows package job uploads as the artifact `QEGTRAIN-windows-x64-payload`. Give an absolute path without a trailing backslash. A relative path is read relative to `installer/`. Quote the whole argument when the path has spaces. |
| `/O<directory>` | no | The output directory. Without it, Setup writes to `Output` below `installer/`. |

The script stops with a message that names the parameter when `/DVersion` or `/DPayload` is missing. It does not check that the payload holds `QEGTRAIN.exe`.

Besides the payload, the compiler reads two files of the repository: `installer/installed-by-setup.ini` and the icon `EGTRAIN/QEGTRAIN/resources/app/egtrain.ico`, which is the icon of Setup.

## What the installer does

- It runs without administrator rights and has one install scope, the current user. The script sets no way to choose another.
- It installs the payload as it is into `%LOCALAPPDATA%\Programs\EGTRAIN` by default, a folder of the user. The folder page of the wizard lets the user choose another folder on a first install.
- It adds the entry `EGTRAIN` to the Start Menu of the user, directly in the Programs folder.
- It offers a desktop shortcut as the task `desktopicon`, which is unchecked by default.
- It adds an uninstall entry that Inno Setup writes under the current user, named after the `AppId` of the script. Windows lists it with the installed apps.
- It associates the file type `.egscene` with the installed program for the current user. Under `HKEY_CURRENT_USER\Software\Classes` it sets the default value of `.egscene` to the identifier `EGTRAIN.Scene`, and it adds that identifier with the display name `EGTRAIN case study`, the first icon of `QEGTRAIN.exe` and the open command `"<installation folder>\QEGTRAIN.exe" "%1"`. Opening a `.egscene` file in Explorer therefore starts `QEGTRAIN.exe` with the file as its only argument, and EGTRAIN opens it as **File > Open Case Study** does, in the window of the same installation when one is open (see [Startup of the window](../docs/guides/command-line.md#startup-of-the-window)). The script registers no other extension, so a `.zip` file is not touched. The portable ZIP registers nothing.
- It writes `installed-by-setup.ini` next to `QEGTRAIN.exe`. Only the installer writes this file, so its presence marks a copy that the installer manages. The portable ZIP does not hold it.
- An install over an older version is an upgrade of that installation: the `AppId` is the same, so Setup uses the installation that the first install made instead of making a second one. It asks Windows to close a running `QEGTRAIN.exe` first.
- The `Scenes` folder inside the installation folder is deleted before each install and replaced by the one in the package, so scenes of an older version do not stay behind. Keep your own cases outside it. Nothing else is deleted before the copy, so other files of an older version stay in the folder.
- Uninstalling removes the files that Setup installed, the shortcuts, the uninstall entry, the file type, and the installation folder when it is then empty. It deletes the default value of `.egscene`, the key of `.egscene` when nothing else is left in it, and the key `EGTRAIN.Scene` with everything below it. A value that another program keeps in the key of `.egscene` stays. It leaves the run output of the program, exported results and case studies. By default the program writes run output below the application data folder of the user, or below `QEGTRAIN_OUTPUT_DIR` when that variable is set. A file that the program creates inside the installation folder stays, and so does the folder.

## Silent install

These Setup switches matter for an unattended install:

| Switch | Effect |
| --- | --- |
| `/VERYSILENT` | No wizard and no progress window. |
| `/SUPPRESSMSGBOXES` | No message boxes. |
| `/NORESTART` | Setup does not restart the computer. |
| `/DIR=<folder>` | Installs into this folder. |
| `/TASKS="desktopicon"` | Creates the desktop shortcut. Without it a silent install creates none. |
| `/LOG=<file>` | Writes a log of the install to the file. |

```text
EGTRAIN-Setup-1.2.3-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS="desktopicon" /LOG="%TEMP%\egtrain-setup.log"
```

## What the script does not do

- It downloads and compiles nothing. It installs the files of the payload directory.
- It does not run the Visual C++ redistributable (`vc_redist.x64.exe`) that the package may hold. Setup copies the file like any other, and running it needs administrator rights.
- It leaves the setting that the user made in Windows for `.egscene` files (**Open with**) as it is.
- It has no signing configuration and no licence page.
- It was written for the package that the Windows package job assembles. Another layout needs another script.

## Updating an installed copy

EGTRAIN does not replace the files of an installed copy itself. When `installed-by-setup.ini` is next to `QEGTRAIN.exe`, **Check for Updates...** names a newer release but offers only the release page (`selfReplacementBlockedReason` in `update/Distribution.cpp`). The in-app update would replace the whole installation folder with the files of the portable package, which hold neither the uninstaller nor `installed-by-setup.ini`, and Windows would keep an uninstall entry whose uninstaller is gone.

An installed copy is therefore updated by running a newer Setup over it.

## Changing the script

The publisher text that Windows shows is set by the lines `AppPublisher` and `AppPublisherURL`. Change those two lines to change it.

Never change the `AppId`. Setup keeps the uninstall entry of an installation under it, so a new value installs a second copy beside the first instead of upgrading it. The `AppId` is a random identifier made once for this script and not derived from anything. In the script it is written with a doubled opening brace, which is how Inno Setup writes a literal brace.

## Test

`tools/release/test_installer_script.py` reads the script and the files it names and checks the rules that keep the installer per user and small, for example the privilege setting, the constants and sections that may appear, the output name against `kAssetPatterns`, what may be deleted before an install and the registry entries of the file type. It does not compile the script and does not run an installer. See [Installer script](../docs/development/build-and-test.md#installer-script) for the checks and the command.
