# Release Testing Checklist

Use this checklist before a student release. Record the tag, commit,
platform, OS version, package name, case-study bundle, result, and linked defect
for every rehearsal.

## Candidate

One rehearsal belongs to one candidate: one commit. Take every file from one
source and never mix files of different runs or releases. The candidate comes
from one of two sources.

- One run of the `Release` workflow, either a manual run or the run of a
  pull request to `production`. The run has these artifacts:
  - `QEGTRAIN-macos-arm64`, holding `EGTRAIN-<version>-macOS-arm64.dmg`
  - `EGTRAIN-windows-x64-installer`, holding `EGTRAIN-Setup-<version>-x64.exe`
  - `EGTRAIN-windows-x64-portable`, holding
    `EGTRAIN-Portable-<version>-x64.zip`
  - `QEGTRAIN-windows-x64-payload`, holding the files of the Windows package
  - `QEGTRAIN-linux-x86_64`, holding `QEGTRAIN-linux-x86_64.AppImage`
  - `EGTRAIN-scenes`, holding the `.egscene` files

  The run page offers each artifact as a download that arrives in an outer zip.
  Unpack that zip once to reach the file named above. The outer zip of
  `QEGTRAIN-windows-x64-payload` holds the files of the Windows package
  themselves. The installer and the portable archive are made from them, so
  rehearse those two and not the payload. A run has no release job, so it has
  no `update-manifest.json` and publishes no release.
- The assets of one published release, made by a push to `production` or by
  a `v*` tag. A release holds the application assets, the case-study bundles
  and `update-manifest.json` that the next section lists, and nothing else. It
  stays a draft until all of them are uploaded. A `v*` tag that contains a
  hyphen is published as a prerelease: the updater never offers it, and its
  version is the number before the hyphen.

A manual run and the run of a pull request build the CMake baseline version, not
a release number. The version shown by **About EGTRAIN** for such a candidate
is therefore the baseline, and the names of the installer and of the portable
archive carry it. Record that version; step 1 says where the entry is.

A fix after a failed row needs a new candidate and a new record.

## Automated release evidence

The release is ready to rehearse only when the version, validation, sanitizer
and the package jobs of the `Release` workflow (macOS, Windows, Finalize Windows
and Linux) are green. The release job needs all of them.
[CI and release branches](build-and-test.md#ci-and-release-branches) describes
what the workflow does.

Expected application assets:

- `EGTRAIN-<version>-macOS-arm64.dmg`
- `EGTRAIN-Setup-<version>-x64.exe`
- `EGTRAIN-Portable-<version>-x64.zip`
- `QEGTRAIN-linux-x86_64.AppImage`

`<version>` is the version of the release without the leading `v`. The releases
up to v1.0.2 have `QEGTRAIN-macos-arm64.zip` for macOS and
`QEGTRAIN-windows-x64.zip` for Windows instead of the disk image and the two
Windows names above.

Expected case-study assets:

- `Netherlands.egscene`
- `Paimpol.egscene`
- `Copenhagen.egscene`
- `Milano_Brescia.egscene`
- `Assignment_Gvc_Gdg_Ut.egscene`
- `Lebanon.egscene`
- `Amsterdam_Hilversum_Student.egscene`

A published release also has `update-manifest.json`. Confirm the workflow
validates all the bundles and publishes exactly the application assets and the
case-study assets above and the update manifest.

What the jobs prove today:

- The Full validation job builds the project with tests on macOS and runs
  CTest and the end-to-end smoke tests. The Sanitizers job runs CTest again in
  a debug build with sanitizers.
- In the Package macOS job, the app has no Homebrew dependency and a valid ad
  hoc signature. The job removes the virtual keyboard input plugin with the Quick
  and QML frameworks, and the WebP and TIFF plugins with their libraries, from
  the app before the signature, and fails when one of them is in the app or when
  the cocoa platform plugin, the SVG icon engine or the macOS style is missing.
  The app holds `scene_tool` and the guide. `scene_tool` validates the Lebanon
  scene of the app, and the app runs Paimpol without the GUI from
  `Contents/Resources/Scenes` to `End of Simulation`. The job starts a copy of
  the app with `tools/release/package_start_smoke.py` (step
  **Start the package**), which first checks that every dependency of the app is
  a system library or a file inside the app and that every library inside the
  app is loaded by some file.
  The job prints the number of files and bytes of the app before and after the
  removal in its job summary. It also packs and validates the seven bundles.
  Then it builds `EGTRAIN-<version>-macOS-arm64.dmg` from the signed app,
  mounts the image read-only, checks that the volume holds only `QEGTRAIN.app`
  and the link `Applications`, that the app in the image and a `ditto` copy of it
  have a valid signature, and that the copy runs Paimpol to `End of Simulation`.
- The Package Windows job checks that these files exist: `QEGTRAIN.exe`,
  `egtrain_update_helper.exe`, `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`,
  `Qt5Charts.dll`, `Qt5Svg.dll`, `Qt5Network.dll`, `platforms/qwindows.dll`,
  `imageformats/qsvg.dll`, `iconengines/qsvgicon.dll`,
  `styles/qwindowsvistastyle.dll`, a `libzmq` DLL, `scene_tool.exe`,
  `Scenes/Paimpol`, `Scenes/Lebanon` and the guide. It fails when the software
  OpenGL, ANGLE or Direct3D compiler libraries or Qt Quick, QML or virtual
  keyboard files are in the package. It prints the number of files and bytes of
  the package in its job summary.
- The Finalize Windows job compiles the installer from that package directory
  with Inno Setup and runs `tools/release/installer_smoke.ps1` on the runner.
  The script checks that the installer asks for no elevation in its manifest.
  A silent install into a chosen folder leaves one uninstall entry for the
  current user and none for the machine, a Start Menu shortcut, no desktop
  shortcut, the files of the package with the same bytes, the marker file
  `installed-by-setup.ini` and one uninstaller, and `package_start_smoke.py`
  starts the installed copy. The install registers the `.egscene` file type for
  the current user, with the installed `QEGTRAIN.exe` as the program that opens
  it, and the uninstall removes it together with the entry, the shortcut and the
  files, and leaves a file beside the folder in place. An install with the desktop
  task creates the desktop shortcut and the uninstall removes it. A second
  installer of the next patch version installed over the first, in the default
  folder, leaves one entry, one folder and the higher version, and replaces the
  `Scenes` folder. A copy of the same directory, started as a portable copy is,
  registers no file type. The job then writes the portable archive from that
  directory, extracts it and starts the extracted program with the same start
  script.
- The Package Linux job extracts the AppImage and checks the executable,
  the update helper, `Scenes/Paimpol`, the icon, `AppRun` and the Qt libraries.
  It then starts the extracted image through its `AppRun` on a virtual display,
  after it has checked that every Qt 5 and ZeroMQ library of the programs and
  plugins comes from the image and not from the runner.
- Each package job also runs the update helper tests
  (`ctest -R '^test_update(helper|preparation)$'`, step **Verify update helper
  transaction**).
- The Package macOS job starts a copy of the app that goes into the disk image, the
  Package Windows job starts a copy of the directory it uploads, and the Package
  Linux job starts the extracted AppImage, all with
  `tools/release/package_start_smoke.py` (step **Start the package**). All three
  also run the CSV export and the PNG export of the Paimpol scene through the
  test hooks of the application. The Windows leg of the CMake workflow starts a
  Windows package assembled in that workflow (step **Start an assembled Windows
  package**). Except for the installer and the portable archive of the Finalize
  Windows job, none of this starts the released file itself, and none of it
  shows how the SVG icons look or typing into text fields.

The Finalize Windows job runs as an administrator on a machine that has the
Visual C++ runtime. It does not show that a standard user installs without an
elevation request, that the package starts on a machine without the runtime, or
how Windows presents the unsigned installer and the entry in the list of
installed apps. The job reads the registry keys of the `.egscene` file type but
opens no file, so it does not show that a double-click on a `.egscene` file in
Explorer opens EGTRAIN (#416); row W7 does. The rows W1 to W7 below cover the rest
of what the job cannot.

None of this shows the GUI student workflow.

## Package contents

| Platform | Download | Holds | Launch |
| --- | --- | --- | --- |
| macOS | `EGTRAIN-<version>-macOS-arm64.dmg` | A disk image with the volume name `EGTRAIN` that holds `QEGTRAIN.app` and `Applications`, a link to the Applications folder, and nothing else. The app holds `scene_tool` (`Contents/MacOS`), `lebanon-case-study.md` (`Contents/Resources`) and the scene folders of the seven cases (`Contents/Resources/Scenes`). It needs macOS 26 or later. | `QEGTRAIN.app`, copied out of the image |
| Windows installer | `EGTRAIN-Setup-<version>-x64.exe` | Installs the files of the Windows package for the current user, by default into `%LOCALAPPDATA%\Programs\EGTRAIN`, with the marker file `installed-by-setup.ini` and an uninstaller. Adds the Start Menu entry `EGTRAIN`, an optional desktop shortcut, an entry in the list of installed apps and the `.egscene` file type for the current user. | The Start Menu entry `EGTRAIN` |
| Windows portable | `EGTRAIN-Portable-<version>-x64.zip` | At the top level of the zip: `QEGTRAIN.exe`, `egtrain_update_helper.exe`, the Qt and ZeroMQ libraries, `platforms/`, `imageformats/`, `Scenes/`, `scene_tool.exe` and `lebanon-case-study.md`. The installed copy holds the same files and, besides them, the marker file and the uninstaller. Unzip into a new empty folder. | `QEGTRAIN.exe` in that folder |
| Linux | `QEGTRAIN-linux-x86_64.AppImage` | One file. It is built from the application, the update helper, `Scenes`, `scene_tool`, `lebanon-case-study.md` and the Qt libraries. Mark it executable. | The AppImage file |

A candidate from a run has the installer and the archive as the artifacts
`EGTRAIN-windows-x64-installer` and `EGTRAIN-windows-x64-portable`. Unpack the
outer zip of each once, as the section Candidate says.

The packages hold scene folders, not `.egscene` bundles. The bundles are
separate files of the candidate.

## Clean-install GUI rehearsal

Run this section on macOS, Windows, and Linux from the downloaded package and a
downloaded bundle of one candidate.
Use a clean machine, VM, or user account without a source checkout, Qt, CMake,
or other development dependencies in the test path. The test account must not
have `QEGTRAIN_` variables set: any `QEGTRAIN_E2E_` variable,
`QEGTRAIN_DISABLE_UPDATES` and `QEGTRAIN_AUTOSTART` switch the update entries
off and are not part of a student launch.

On Windows there are two downloads, and each one gets this section. Row W2 below
installs the installer and runs the steps on the installed copy. Row W6 unzips
the portable archive and runs the steps and the R rows on that copy.
Rows R9 and R10 replace the installation, and an installed copy has no update
path of its own yet (#405), so they are not run on the installed copy. Both
copies keep their settings in the profile of the account, not in their folder,
so the question of R7 is asked at the first start in W2 and not again in W6.

**Before step 2.** Make a byte-for-byte copy of the downloaded bundle in another
folder and note the modification time of the original. While the downloaded
bundle is the open case, **File > Save Scene** and the **Save** button of the
**Unsaved Scene** message write into the open bundle. The edit of step 4 is
therefore saved only through step 5, **Save Case Study As...**.

Installed examples are protected separately: Save asks for a new bundle in a
user folder. This does not protect an ordinary downloaded bundle from an
in-place save. Row R11 checks the installed-example path.

1. Launch the downloaded application through the platform's normal GUI path.
   On macOS that path is: open the disk image, drag `QEGTRAIN.app` onto
   `Applications`, eject the image and open the copy in the Applications folder.
   Record:
   - on macOS, what the window of the disk image shows;
   - the version shown by **About EGTRAIN**, in the **Help** menu or, on macOS,
     the application menu;
   - any operating system warning shown at the first launch, with its text. The
     macOS package has an ad hoc signature only, so the system is expected to
     ask for confirmation there. Do not assume a warning on Windows or Linux;
     record what appears;
   - the title of every dialog shown before the main window can be used;
   - anything you had to install or change to make the package start. On
     Windows, say whether the Visual C++ runtime was installed before the test.
     The package is built with `windeployqt --compiler-runtime` and the package
     job lists no runtime file, so the job does not show whether a clean machine
     starts without it.

   After the main window appears, the application shows the case chooser
   **Open a Case**. Its group **Bundled cases** lists the cases found in a
   `Scenes` folder next to the executable or in the working directory, in
   `../Resources/Scenes` or in `../share/EGTRAIN/Scenes` relative to the
   executable. Note what the chooser lists. Close it before step 2 with the
   button that keeps the current case: **Continue** when a case is loaded and
   **Cancel** when none is.
2. Open a downloaded `.egscene` through **File > Open Case Study...**.
3. Confirm the network renders. Show Loaded Data with **View > Loaded Data**
   (the entry is checked while the dock is shown) and confirm that it shows
   identity, versions, category counts, scenarios, validation, runtime, and
   result readiness.
4. Select or edit a scenario and confirm the run review names that scenario.
5. Use **Save Case Study As...** to write a working copy outside the package.
   The dialog proposes the downloaded bundle itself: give the copy a new name
   in another folder.
   Confirm the downloaded source bundle is unchanged. Compare the original with
   the copy from before step 2 and check the modification time of the original
   against the one you noted. On macOS and Linux, run `cmp <original> <copy>`,
   which prints nothing when the files are equal. On Windows, run
   `fc /b <original> <copy>` in a command prompt, not in PowerShell, where `fc`
   is another command; it reports no differences when the files are equal.
6. Run a short simulation from the working copy: choose **Simulation > Run**,
   then **Run simulation** in the review window.
7. Open timetable, delay, speed, and blocking-time results where the case
   supports them. Confirm timetable output separates planned and simulated
   arrival and departure values.
8. Export one CSV and one PNG to a user-writable directory.
9. Quit, relaunch, and reopen the working copy. Confirm saved canonical and
   scenario edits remain. Compare the downloaded source bundle with its copy
   again, as in step 5, and check its modification time.

## Further checks on the packages built today

Run these rows on each platform. R7 is seen at the first launch of step 1, R3
runs after step 9, and R9 runs last, because it replaces the installation. Run
R2 to R6 with the working copy open or with no pending edit, never with the
downloaded bundle open and edited.

| Row | What to do | What must be seen or recorded | Platforms |
| --- | --- | --- | --- |
| R1 Run from a second location | Move or copy the unpacked package to another writable folder and repeat steps 1 and 2. On macOS, copy `QEGTRAIN.app` with Finder or `ditto`, not file by file out of the app. On Windows, copy the folder that holds `QEGTRAIN.exe`. On Linux, copy the AppImage. | The application starts from the new location and opens a bundle. | All |
| R2 Drop a bundle onto the open window | Drag one `.egscene` file from the file manager onto the open main window. | It opens like **File > Open Case Study...**. The application accepts exactly one local `.egscene` file. | All |
| R3 Unsaved changes | Run it after step 9, when the open case is the working copy and not the downloaded bundle. Change a scenario value and choose **File > Open Case Study...**. In the file dialog choose another copy of a bundle, never the downloaded bundle. The **Unsaved Scene** message follows the choice of a file. Then repeat with **File > Quit** (on macOS, **Quit** in the application menu). Make the change again before each repeat, and reopen the working copy after **Discard**. | The message offers **Save**, **Discard** and **Cancel**. Answer **Cancel** first and record that the edit and the open case stay. For the open request the status bar then shows `Open canceled. Current scene retained.` Then answer **Save** and **Discard** once each, and record what happened to the working copy. Afterwards the downloaded bundle must still equal its copy. | All |
| R4 Damaged file | Rename a plain text file to `something.egscene` and open it with **File > Open Case Study...**. | Record the message title and text, and whether the case that was open before is still shown. The application must not crash and must not change the file. | All |
| R5 Non-ASCII path | Put a copy of a bundle in a folder whose name and file name contain accented letters (for example é or ü) and one character outside the Latin alphabet. Open it, save a working copy into such a folder, and reopen the copy. | The bundle opens, the copy is saved, and the copy reopens. Record what appears. | All |
| R6 Assignment bundle | Open `Assignment_Gvc_Gdg_Ut.egscene`. | Its current data loads. Known station and rolling-stock fidelity gaps belong to #182, #228, and #181; they are not packaging failures. | All |
| R7 First start | Launch as in step 1 on an account whose update question has never been answered, and close the case chooser. | A question **Automatically Check for EGTRAIN Updates?** appears once, after the case chooser has been closed, with the buttons **Check Automatically** and **Don't Check Automatically**. Record the choice. **Help** then shows **Check for Updates...** and **Automatically Check for Updates**. | All |
| R8 Manual update check | Choose **Help > Check for Updates...**. | The message reads `EGTRAIN <version> is up to date.`, or a dialog titled `EGTRAIN <version> is Available` offers a newer stable release with the buttons **Update and Restart** (only where the installation can update itself), **Open Release Page**, **Later** and **Stop Checking**. A check that fails shows a warning titled `Check for Updates`. Record the text. | All |
| R9 In-app update | Run it last on a platform. Meet the precondition below, then choose **Update and Restart**. | The application restarts and **About EGTRAIN** shows the newer version. The hidden folder `.qegtrain-update-*` next to the installation is gone. On Windows the update helper cannot delete itself, so the application removes the rest about 20 seconds after it started. One backup of the previous installation, named like the installation plus `.egtrain-old`, stays next to it until the next update. If the dialog says the installation is not writable although the folder is writable, record the path and the text. | All |
| R10 Installation that cannot be updated in place | First confirm the precondition of R9 in a writable folder. Then place the installation where the folder that holds it cannot be written by the test account. That folder is the parent of `QEGTRAIN.app`, of the Windows application folder or of the AppImage. Choose **Help > Check for Updates...** again. | The update dialog has no **Update and Restart** button. Its text says `The packaged installation is not writable. Use the release page to update manually.` On macOS, a copy that runs from a mounted disk image shows `EGTRAIN is running from a read-only location, such as a disk image. Move EGTRAIN to the Applications folder first. Use the release page to update manually.` instead. | All |
| R11 Save an installed example | Copy the installed scene folder before testing, then open the installed example through **Open Case**, edit it, and choose **Save Scene**. Cancel once, then save a new `.egscene` outside the installation. Reopen and edit the installed example before repeating through **Save** in **Unsaved Scene**. Try an installed folder as the destination of **Save Scene As Folder...**, and a file inside it through **Save Case Study As...**. | The dialog starts in the user's home folder. Cancel retains the edit and current case; a successful save makes the new bundle current. Installed destinations are rejected. The original folder's file paths and bytes remain unchanged. A later edit saves in place to the external bundle. | All |

Notes on the update rows:

- The update check looks at the latest stable release. A draft or a prerelease
  is never offered. **Update and Restart** is offered only for a stable release
  that is newer than the running version, holds `update-manifest.json` and the
  package of the platform, and only where the installation can update itself.
  [Application updates](../guides/scenes-and-application.md#application-updates)
  describes the update.
- A candidate that carries the baseline version is offered the latest published
  stable release when that is newer. Do not update it in place before the other
  rows of that platform are done; R9 does that on purpose, last. With automatic
  checks on, the same dialog also appears at the start of the application;
  choose **Later** until R9.
- **Stop Checking** switches automatic checks off. Use it only to record that
  button, and only if R7 does not have to be repeated on that account.
- Precondition of R9 and R10: **Help > Check for Updates...** on the
  installation under test offers a newer stable release with the button
  **Update and Restart**. The installation under test is the candidate itself
  when its version is lower than the latest stable release. That is so for a
  manual run whenever a stable release newer than the baseline exists. Otherwise
  install an older stable release from its downloaded asset: it is the one
  exception to the rule of one source, so record its tag. Put the
  installation in a folder the test account can write to. Run R10 before R9, or
  on a separate copy of the package, because R9 replaces the installation.
- R9 installs the latest published stable release, not the candidate. It shows
  the update path of the installed version and says nothing about the contents
  of a manual candidate.
- On Windows run R9 and R10 on the portable copy. In an installed copy,
  **Update and Restart** replaces the installation folder with the portable
  package. That package has no uninstaller, so the entry in the list of
  installed apps can no longer remove the copy; update an installed copy with a
  newer Setup (W5). The older stable release for R9 on Windows is
  `QEGTRAIN-windows-x64.zip`, a portable archive too.
- Mark R9 or R10 not applicable, with a reason for this case, when no newer
  stable release is offered, when no older stable release that has the update
  entries can be installed for R9, or when the account cannot be denied write
  access for R10.

## Windows installer and portable archive

Run these rows on Windows with the installer and the portable archive of the
candidate, and mark them not applicable on macOS and Linux. Use an account that
is not an administrator, on a machine without development tools and without a
copy of EGTRAIN. W2 to W5 follow each other on that account, and W7 runs between W2 and W3. W5 needs the Setup
of an older version: it is the one exception to the rule of one source besides
R9, so record where it came from. Run W6 after the last uninstall, so that the
list of installed apps shows only what the archive itself adds.

| Row | What to do | What must be seen or recorded | Platforms |
| --- | --- | --- | --- |
| W1 Windows assets | Open the release page or the run page of the candidate. For a published release also open `update-manifest.json`. | `EGTRAIN-Setup-<version>-x64.exe` and `EGTRAIN-Portable-<version>-x64.zip` are both there with the version of the candidate in their names. In the manifest of a published release the entry `windows-x64` names the archive and the entry `windows-x64-installer` names the installer, and the `size` of each equals the size of the downloaded file. | Windows |
| W2 Install | Double-click the Setup. Keep the default folder and leave the desktop shortcut box unticked. When it has finished, start EGTRAIN from the Start Menu and run the steps 1 to 9 of the clean-install rehearsal on this copy. | No prompt asks for an administrator password or for confirmation to run as administrator. Record any warning before Setup starts, with its text. The folder page offers `%LOCALAPPDATA%\Programs\EGTRAIN`. The Start Menu has the entry `EGTRAIN`. The list of installed apps in Windows Settings has `EGTRAIN`: record the version, which must be the one of the candidate, and the publisher. There is no desktop shortcut. Nothing is installed below `Program Files`. | Windows |
| W3 Uninstall | Note the path of the working copy and of the exported CSV and PNG files, which lie outside the installation folder. Remove `EGTRAIN` from the list of installed apps in Windows Settings. | The entry, the Start Menu entry and any desktop shortcut are gone. The installation folder is gone, or holds only files that you or the program created in it. The working copy, the exported files, the downloaded bundles and the run output below the application data folder are unchanged. A `.egscene` file no longer opens in EGTRAIN. | Windows |
| W4 Reinstall | Run the same Setup again, and this time tick the desktop shortcut box. Start EGTRAIN from the desktop shortcut. | It installs as in W2, with one entry in the list of installed apps and one Start Menu entry. The shortcut starts EGTRAIN. The question of R7 is not asked again, and the working copy opens: the settings and the files outside the folder survived W3. | Windows |
| W5 Upgrade | Uninstall as in W3. Install the older Setup, start EGTRAIN and leave it open, then run the Setup of the candidate over it. When it has finished, start EGTRAIN, open **About EGTRAIN**, and uninstall once more as in W3. | The first uninstall removes the desktop shortcut of W4 with the rest. Setup offers to close the running EGTRAIN and finishes when it is closed. Afterwards the list of installed apps has one `EGTRAIN` entry with the version of the candidate, there is one Start Menu entry, the installation folder is the same and no second folder named like it stands beside it, and **About EGTRAIN** shows the version of the candidate. The last uninstall removes everything as in W3. Mark W5 not applicable, with the reason, when no older Setup exists: the Finalize Windows job has installed a second Setup over the first on the runner. | Windows |
| W6 Portable archive | Unzip `EGTRAIN-Portable-<version>-x64.zip` into a new empty folder, for example with **Extract All** in Explorer. Start `QEGTRAIN.exe` by double-click. Run the steps 1 to 9 of the clean-install rehearsal and the R rows on this copy, except R7, which W2 has seen. | No prompt asks for administrator rights. Record any warning at the first start, with its text. The list of installed apps and the Start Menu show no EGTRAIN, and no shortcut was made. The folder has no `installed-by-setup.ini` and no uninstaller. Deleting the folder after closing EGTRAIN removes the copy. | Windows |
| W7 Open a case study by double-click | Run it right after W2. Close EGTRAIN and double-click a downloaded `.egscene` file in Explorer. With EGTRAIN open, change a value and double-click a second `.egscene` file. | Explorer shows the type name `EGTRAIN case study` and the EGTRAIN icon for the file. The first file opens in EGTRAIN. The second opens in the window that is already open, after the question about unsaved changes, and no second window appears. If Windows asks which program to use, record the question. | Windows |

## Not available yet

The release does not build these today, so they have no subject.

| Check | What the release has today | Status |
| --- | --- | --- |
| macOS Developer ID signing and notarization: first open without a system warning | An ad hoc signature only (#404). | No row until the release provides it |
| Windows binary signature | None (#402). | No row until the release provides it |
| Open a bundle from the file manager or the command line, with the application closed and with it running | **File > Open Case Study...**, a drop onto the window and the `--scene` option (#416). | No row until the release provides it |
| Update of an installer-managed copy as against a portable copy | **Update and Restart** replaces an installed copy with the portable package, so R9 and R10 are the only update rows and run on the portable copy (#405). | No row until the release provides it |

These rows are not part of the gate. They are added to the table of the record
when the release provides the asset.

## Result record

Copy [the template](release-rehearsal-template.md) to
`docs/development/release-rehearsal-<date>.md`, with the date of the rehearsal,
and fill it. Link every failed row to one focused defect issue. The record of
[2026-08-23](release-rehearsal-2026-08-23.md) is an earlier filled record of a
macOS rehearsal of an older candidate.

The release gate passes only after every required row passes on all three
platforms or is marked not applicable with a case-specific reason. The rows W1
to W7 belong to Windows only. Open one focused defect for each failed root cause
instead of adding implementation work to issue #74.
