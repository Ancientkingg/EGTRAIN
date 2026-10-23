# Installing EGTRAIN

This page says which file to download for your system, how to install and start EGTRAIN, what macOS and Windows show at the first start, how to check a download and how to remove EGTRAIN. [Getting started](getting-started.md) continues with a first run.

## Download and start

1. Download the package for your system from the [releases page](https://github.com/Ancientkingg/EGTRAIN/releases): `EGTRAIN-<version>-macOS-arm64.dmg` (Apple silicon Macs with macOS 26 or later; [details](#macos-disk-image)), `EGTRAIN-Setup-<version>-x64.exe` or `EGTRAIN-Portable-<version>-x64.zip` (64-bit Windows; [choose one](#windows-installer-and-portable-archive)) or `QEGTRAIN-linux-x86_64.AppImage` (64-bit Linux). `<version>` is the version of the release. Download a case file from the same page too, such as `Paimpol.egscene`.
2. Start the application.
   - macOS: open the disk image, drag `QEGTRAIN.app` onto `Applications` in its window, and open EGTRAIN from there. macOS refuses the first start of an ad hoc signed app; the [first start](#macos-disk-image) below says what to do.
   - Windows: run the installer and open **EGTRAIN** from the Start Menu, or unzip the portable archive into a new, empty folder and run `QEGTRAIN.exe` from it. Keep that folder intact. The Windows files are not code signed, so Windows warns before the installer starts; the [first start](#first-start-of-an-unsigned-build) below says what to do, and [Checking a download](#checking-a-download) says how to check a download.
   - Linux: make the AppImage executable and run it. It needs glibc 2.38 or later, as on Ubuntu 24.04, and takes the OpenGL and EGL libraries `libGL.so.1`, `libOpenGL.so.0` and `libEGL.so.1` from the system. On a minimal system that reports one of them missing, install `libgl1`, `libopengl0`, `libegl1`, `libharfbuzz0b` and `libfontconfig1` first (the package names of Ubuntu). The AppImage also needs the FUSE 2 library: when it stops with `AppImages require FUSE to run.`, install `libfuse2t64` (Ubuntu 24.04), or start it with `./QEGTRAIN-linux-x86_64.AppImage --appimage-extract-and-run`.

## macOS disk image

`EGTRAIN-<version>-macOS-arm64.dmg` holds `QEGTRAIN.app` and `Applications`, a shortcut to the Applications folder. It needs an Apple silicon Mac with macOS 26 or later. Releases up to v1.0.2 offer the app as `QEGTRAIN-macos-arm64.zip` instead.

- **Install.** Open the image and drag `QEGTRAIN.app` onto `Applications`, then eject the image. The app is self-contained and needs no installer, so a copy in any other folder that you can write to works the same way. Run EGTRAIN from a copy and not from the image: an app on the image is read-only and cannot update itself.
- **First start.** Unless the release notes say that the app is notarized, expect that it is not notarized by Apple and that macOS refuses the first start with `"QEGTRAIN" Not Opened`, which offers **Move to Trash** and **Done**. To allow the app, choose **Done**, open **System Settings > Privacy & Security** and, in the section **Security**, choose **Open Anyway** for `"QEGTRAIN" was blocked to protect your Mac.` Choose **Open Anyway** again in `Open "QEGTRAIN"?`. macOS then asks for the name and password of an administrator. These are the dialogs of macOS 26.6; other versions may word them differently. This is the normal macOS override. Do not turn Gatekeeper off or remove the download mark with a command.
- **Notarized releases.** When the release notes say that the app is signed with a Developer ID and notarized, macOS is expected to ask only for the usual confirmation for an app that was downloaded. [First start of a notarized build](#first-start-of-a-notarized-build) has the details.
- **Open a case.** Double-click an `.egscene` file in Finder to open it in EGTRAIN, or Control-click it and choose **Open With** and `QEGTRAIN`. When EGTRAIN is open, the file opens in its window. [Open by double-click](opening-a-case-study.md#open-by-double-click) has the details.
- **Update.** For a copy outside the image, **Help > Check for Updates...** offers **Update and Restart** when a newer release exists.
- **Remove.** Quit EGTRAIN and drag `QEGTRAIN.app` to the Trash. Your cases, your exported files and the settings of EGTRAIN stay; [Removing EGTRAIN](#removing-egtrain) has the details.
- **Inside the app.** The bundled cases are in `QEGTRAIN.app/Contents/Resources/Scenes` and the command-line tool is `Contents/MacOS/scene_tool` (see [Command-line options and scene_tool](command-line.md)). The image shows no other file.

### First start of an ad hoc build

This was observed on 2026-10-16 on a clean macOS 26.6.2 with the setting **Allow applications from** at **App Store &
Known Developers**, for a disk image that carried the download mark that a browser sets. Opening the image shows a
Finder window named `EGTRAIN` with `QEGTRAIN` and `Applications`, and no warning. After the app is copied to the
Applications folder, opening it from there shows:

1. A dialog titled `"QEGTRAIN" Not Opened`. Its text is
   `Apple could not verify "QEGTRAIN" is free of malware that may harm your Mac or compromise your privacy.`
   Its buttons are **Move to Trash** and **Done**. It has no **Open** button.
2. **System Settings > Privacy & Security**, in the section **Security**, shows
   `"QEGTRAIN" was blocked to protect your Mac.` with the button **Open Anyway**.
3. **Open Anyway** opens a dialog titled `Open "QEGTRAIN"?` with the buttons **Move to Trash**, **Open Anyway** and
   **Done**.
4. **Open Anyway** in that dialog asks for an administrator's name and password:
   `Enter an administrator's username and password to allow this.` The exception needs an administrator. What follows
   the password was not observed.

This is the normal macOS override for an app from an unidentified developer, and the only way to open an ad hoc build
that this project documents. No workflow and no release script removes the download mark (the quarantine attribute) or
turns Gatekeeper off, and no document tells a user to, because that lowers the protection of the whole Mac.

### First start of a notarized build

The app and the image carry a Developer ID signature and a ticket from Apple that is stapled to each. A user who opens
the image, drags the app onto `Applications` and opens it is expected to see no dialog beyond the confirmation that
macOS asks for an app that was downloaded, and to need no visit to System Settings and no administrator password. No
release of EGTRAIN has been built in this mode, so this has not been observed.

### Checking a macOS download

Run these on the copy of the app in the Applications folder. They do not start the app.

```bash
codesign -dvv /Applications/QEGTRAIN.app
xcrun stapler validate /Applications/QEGTRAIN.app
```

`codesign` shows `Signature=adhoc` for an ad hoc build. For a Developer ID build it shows a line
`Authority=Developer ID Application:` with the name of the signer, and a `TeamIdentifier`. `stapler validate` succeeds
only when the app carries a ticket and fails for an ad hoc build.

## Windows installer and portable archive

The installer and the portable archive hold the same application files. Choose the installer to have EGTRAIN in the Start Menu and in the list of installed apps. Choose the archive to run EGTRAIN from a folder without installing anything. Releases up to v1.0.2 have no installer and offer the archive as `QEGTRAIN-windows-x64.zip`.

- **Install.** `EGTRAIN-Setup-<version>-x64.exe` installs for the current user and needs no administrator rights. The default folder is `%LOCALAPPDATA%\Programs\EGTRAIN`, and the wizard offers another folder on a first install. It adds the Start Menu entry **EGTRAIN**, an entry in the list of installed apps in Windows Settings and, only if you tick it, a desktop shortcut. It also registers EGTRAIN for `.egscene` files of your user, so a double-click opens them in EGTRAIN unless you chose another program for them with **Open with**.
- **First start.** The Windows files are not code signed. On Windows 11 in October 2026, with files that carried the download mark, starting the installer showed the dialog `Windows protected your PC` with the text `Microsoft Defender SmartScreen prevented an unrecognized app from starting. Running this app might put your PC at risk.` Its only buttons are a **More info** link and **Don't run**. **More info** shows `App: EGTRAIN-Setup-<version>-x64.exe` and `Publisher: Unknown publisher`, and adds **Run anyway**, which starts Setup. No administrator prompt follows. The dialog comes back each time you start Setup. The portable `QEGTRAIN.exe`, extracted with **Extract All**, started without a warning in the same test. Other Windows versions may word this differently. Do not turn SmartScreen off. [First start of an unsigned build](#first-start-of-an-unsigned-build) has the details.
- **Upgrade.** Run the Setup of the newer version over the installed copy. It updates that installation and does not add a second one. Setup offers to close EGTRAIN if it is running. It replaces the `Scenes` folder of the installation, so keep your own cases outside it. **Update and Restart** in an installed copy does the same for you: it downloads the Setup of the newer version, closes EGTRAIN, runs the Setup without windows and starts EGTRAIN again, which then tells you which version runs.
- **Uninstall.** Remove EGTRAIN from the list of installed apps in Windows Settings. [Removing EGTRAIN](#removing-egtrain) says what this removes and what stays.
- **Portable archive.** `EGTRAIN-Portable-<version>-x64.zip` needs no installation and no administrator rights. Unzip it into a new, empty folder and run `QEGTRAIN.exe`. It adds no Start Menu entry, no shortcut, no entry in the list of installed apps and no setting for `.egscene` files. To remove it, close EGTRAIN and delete the folder ([Removing EGTRAIN](#removing-egtrain)). To use a newer version, unzip the newer archive into another new folder, or use **Update and Restart** in EGTRAIN, which does not keep files that you added to the folder. A portable copy of v1.0.3 or earlier cannot update itself to a release that is built with Qt 6, as every release after v1.0.3 is: unzip the archive of the newer release into a new folder instead.

### First start of an unsigned build

This was observed on 2026-10-16 on a clean Windows 11 (build 26300, ARM64, running the x64 files through Windows'
emulation) as a standard user, for files that carried the download mark that a browser sets (`ZoneId=3`). Double-clicking `EGTRAIN-Setup-1.0.0-x64.exe` shows:

1. A dialog headed `Windows protected your PC`. Its text is
   `Microsoft Defender SmartScreen prevented an unrecognized app from starting. Running this app might put your PC at risk.`
   It has a **More info** link and a **Don't run** button.
2. After **More info**, the dialog shows `App: EGTRAIN-Setup-1.0.0-x64.exe` and `Publisher: Unknown publisher`, with the
   buttons **Run anyway** and **Don't run**.
3. **Run anyway** starts Setup. No prompt for an administrator follows.

The dialog appears at every start of Setup. The portable `QEGTRAIN.exe`, extracted with **Extract All** from the ZIP,
carried the download mark too and started without any warning. The wording and the options depend on the Windows
version, and whether a warning appears may depend on other checks of Windows that this project has not observed.

**Run anyway** is the normal Windows override for an app without a publisher. Do not turn SmartScreen off: that lowers
the protection of the whole PC.

A valid signature would show who signed a file. Whether Windows still warns for a signed program is not known.

### Checking a download

These checks need a Windows machine with PowerShell. Unpack the ZIP first for the three program files, and check the
installer as downloaded. Run the commands for each of `QEGTRAIN.exe`, `egtrain_update_helper.exe`, `scene_tool.exe` and
the installer, with the file name changed.

```powershell
$sig = Get-AuthenticodeSignature -FilePath .\QEGTRAIN.exe
$sig.Status
$sig.StatusMessage
$sig.SignerCertificate.Subject
$sig.SignerCertificate.Issuer
$sig.TimeStamperCertificate.Subject
```

For a signed release, `Status` is `Valid`, the `Subject` shows the signer name and the `Issuer` shows the issuer name of
the eligibility record on the code signing page of the developer documentation (both rows read Not recorded until the
owner fills them), and the time stamper line is not empty.
`StatusMessage` gives the status in words. For an unsigned file, `Status` is `NotSigned` and the three certificate lines
are empty. Any other status means the file must not be trusted.

In a shell where `signtool` is available (it comes with the Windows SDK), run `signtool verify /pa /v QEGTRAIN.exe`; use
the same command for each file. The output shows the signing chain and whether the signature is timestamped. An exit
code of 0 means that it verified. Compare the Issued to and Issued by lines of the signing certificate with the record
and ignore the other lines.

`Valid` means that the file is unchanged since it was signed and that the chain is trusted on this machine. It does not
mean that the program is free of defects or that it matches a given source revision. The checks apply once a release is
signed. Until signing is enabled and a release is built with it, the Windows downloads show `NotSigned`.

## Removing EGTRAIN

Close EGTRAIN first, then remove the copy that you installed:

- **Windows Setup.** In Windows Settings, find EGTRAIN in the list of installed apps (Apps > Installed apps on Windows 11) and choose Uninstall. This removes the program files, the shortcuts, the entry in the list and the `.egscene` setting that the installer made. A file that you or the program put into the installation folder stays, and so does the folder.
- **Windows portable copy.** Delete the folder that you unzipped the archive into.
- **macOS.** Drag `QEGTRAIN.app` from the Applications folder to the Trash.
- **Linux.** Delete the AppImage file.

After an update with **Update and Restart**, the previous version stays next to the installation under the same name followed by `.egtrain-old`, such as `QEGTRAIN.app.egtrain-old`. EGTRAIN removes it about 20 seconds after the new version has started. EGTRAIN 1.0.4 and earlier do not remove it, so a copy that runs one of those versions keeps it until its next update. If it is there, delete it together with the app. A copy installed with Setup has none.

Removing the program leaves the settings of EGTRAIN and the files that it keeps for you, such as the run output in the [output folder](command-line.md#output-folder). To leave nothing behind, delete these as well:

| System | Settings | Data |
| --- | --- | --- |
| Windows | The registry key `HKCU\Software\EGTRAIN` | `%APPDATA%\EGTRAIN` (the run output) and `%LOCALAPPDATA%\EGTRAIN` (recovery copies, crash notes and other working files) |
| macOS | `~/Library/Preferences/com.egtrain.EGTRAIN.plist`, and `~/Library/Preferences/com.egtrain.simulator.plist`, where macOS keeps the state of the open and save dialogs | `~/Library/Application Support/EGTRAIN` (the run output, recovery copies, crash notes and other working files) |
| Linux | `~/.config/EGTRAIN` | `~/.local/share/EGTRAIN` (the same files as on macOS) |

Cases and exported files that you saved elsewhere are yours, and they stay where you saved them.
