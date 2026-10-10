# Release Testing Checklist

Use this checklist before a student release. Record the tag, commit,
platform, OS version, package name, case-study bundle, result, and linked defect
for every rehearsal.

## Candidate

One rehearsal belongs to one candidate: one commit. Take every file from one
source and never mix files of different runs or releases. The candidate comes
from one of two sources.

- One run of the `Release` workflow, either a manual run or the run of a
  pull request to `production`. The run has four artifacts:
  - `QEGTRAIN-macos-arm64`, holding `QEGTRAIN-macos-arm64.zip`
  - `QEGTRAIN-windows-x64-payload`, holding the files of the Windows package
  - `QEGTRAIN-linux-x86_64`, holding `QEGTRAIN-linux-x86_64.AppImage`
  - `EGTRAIN-scenes`, holding the seven `.egscene` files

  The run page offers each artifact as a download that arrives in an outer zip.
  Unpack that zip once to reach the file named above. The outer zip of
  `QEGTRAIN-windows-x64-payload` holds the files of the Windows package
  themselves: unpack it into a new empty folder. A run has no release job, so it
  has no `update-manifest.json`, no `QEGTRAIN-windows-x64.zip` (the release job
  writes that archive from the Windows package) and publishes no release.
- The assets of one published release, made by a push to `production` or by
  a `v*` tag. A release has exactly eleven files: the three packages, the seven
  bundles and `update-manifest.json`. It stays a draft until all of them are
  uploaded. A `v*` tag that contains a hyphen is published as a prerelease: the
  updater never offers it, and its version is the number before the hyphen.

A manual run and the run of a pull request build the CMake baseline version, not
a release number. The version shown by **About EGTRAIN** for such a candidate
is therefore the baseline. Record that version; step 1 says where the entry is.

A fix after a failed row needs a new candidate and a new record.

## Automated release evidence

The release is ready to rehearse only when the version, validation, sanitizer
and the three package jobs of the `Release` workflow are green. The release job
needs all of them. [CI and release branches](build-and-test.md#ci-and-release-branches)
describes what the workflow does.

Expected application assets:

- `QEGTRAIN-macos-arm64.zip`
- `QEGTRAIN-windows-x64.zip`
- `QEGTRAIN-linux-x86_64.AppImage`

Expected case-study assets:

- `Netherlands.egscene`
- `Paimpol.egscene`
- `Copenhagen.egscene`
- `Milano_Brescia.egscene`
- `Assignment_Gvc_Gdg_Ut.egscene`
- `Lebanon.egscene`
- `Amsterdam_Hilversum_Student.egscene`

A published release also has `update-manifest.json`, eleven files in all.
Confirm the workflow validates all seven bundles and publishes exactly the three
application assets, the seven case-study assets and the update manifest.

What the jobs prove today:

- The Full validation job builds the project with tests on macOS and runs
  CTest and the end-to-end smoke tests. The Sanitizers job runs CTest again in
  a debug build with sanitizers.
- In the Package macOS job, the app has no Homebrew dependency and a valid ad
  hoc signature. The job removes the virtual keyboard input plugin with the Quick
  and QML frameworks, and the WebP and TIFF plugins with their libraries, from
  the app before the signature, and fails when one of them is in the app or when
  the cocoa platform plugin, the SVG icon engine or the macOS style is missing.
  The package holds the app, `scene_tool`, `Scenes/Lebanon` and the guide.
  `scene_tool` validates the packaged Lebanon scene, and the packaged app runs
  Paimpol without the GUI from `Contents/Resources/Scenes` to
  `End of Simulation`. The job starts a copy of the app with
  `tools/release/package_start_smoke.py` (step **Start the package**), which
  first checks that every dependency of the app is a system library or a file
  inside the app and that every library inside the app is loaded by some file.
  The job prints the number of files and bytes of the app before and after the
  removal in its job summary. It also packs and validates the seven bundles.
- The Package Windows job checks that these files exist: `QEGTRAIN.exe`,
  `egtrain_update_helper.exe`, `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`,
  `Qt5Charts.dll`, `Qt5Svg.dll`, `Qt5Network.dll`, `platforms/qwindows.dll`,
  `imageformats/qsvg.dll`, `iconengines/qsvgicon.dll`,
  `styles/qwindowsvistastyle.dll`, a `libzmq` DLL, `scene_tool.exe`,
  `Scenes/Paimpol`, `Scenes/Lebanon` and the guide. It fails when the software
  OpenGL, ANGLE or Direct3D compiler libraries or Qt Quick, QML or virtual
  keyboard files are in the package. It prints the number of files and bytes of
  the package in its job summary.
- The Package Linux job extracts the AppImage and checks the executable,
  the update helper, `Scenes/Paimpol`, the icon, `AppRun` and the Qt libraries.
  It then starts the extracted image through its `AppRun` on a virtual display,
  after it has checked that every Qt 5 and ZeroMQ library of the programs and
  plugins comes from the image and not from the runner.
- Each package job also runs the update helper tests
  (`ctest -R '^test_update(helper|preparation)$'`, step **Verify update helper
  transaction**).
- The Package macOS job starts a copy of the app in the directory it zips, the
  Package Windows job starts a copy of the directory it uploads, and the Package
  Linux job starts the extracted AppImage, all with
  `tools/release/package_start_smoke.py` (step **Start the package**). All three
  also run the CSV export and the PNG export of the Paimpol scene through the
  test hooks of the application. The Windows leg of the CMake workflow starts a
  Windows package assembled in that workflow (step **Start an assembled Windows
  package**). None of this starts the released file itself, and none of it shows
  how the SVG icons look or typing into text fields.

None of this shows the GUI student workflow.

## Package contents

| Platform | Download | Holds | Launch |
| --- | --- | --- | --- |
| macOS | `QEGTRAIN-macos-arm64.zip` | Unpacks to the folder `QEGTRAIN-Lebanon` with `QEGTRAIN.app`, `scene_tool`, `Scenes/Lebanon` and `lebanon-case-study.md`. The scene folders of the seven cases are inside the app, under `Contents/Resources/Scenes`. | `QEGTRAIN.app` |
| Windows | `QEGTRAIN-windows-x64.zip` | At the top level of the zip: `QEGTRAIN.exe`, `egtrain_update_helper.exe`, the Qt and ZeroMQ libraries, `platforms/`, `imageformats/`, `Scenes/`, `scene_tool.exe` and `lebanon-case-study.md`. Unzip into a new empty folder. | `QEGTRAIN.exe` in that folder |
| Linux | `QEGTRAIN-linux-x86_64.AppImage` | One file. It is built from the application, the update helper, `Scenes`, `scene_tool`, `lebanon-case-study.md` and the Qt libraries. Mark it executable. | The AppImage file |

A candidate from a run has no `QEGTRAIN-windows-x64.zip`. Its artifact
`QEGTRAIN-windows-x64-payload` holds the same files at the top level: unpack
the download into a new empty folder and launch `QEGTRAIN.exe` there.

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

**Before step 2.** Make a byte-for-byte copy of the downloaded bundle in another
folder and note the modification time of the original. While the downloaded
bundle is the open case, **File > Save Scene** and the **Save** button of the
**Unsaved Scene** message write into the open bundle. The edit of step 4 is
therefore saved only through step 5, **Save Case Study As...**.

1. Launch the downloaded application through the platform's normal GUI path.
   Record:
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
| R1 Run from a second location | Move or copy the unpacked package to another writable folder and repeat steps 1 and 2. On macOS, copy the `QEGTRAIN-Lebanon` folder with Finder or `ditto`, not file by file out of the app. On Windows, copy the folder that holds `QEGTRAIN.exe`. On Linux, copy the AppImage. | The application starts from the new location and opens a bundle. | All |
| R2 Drop a bundle onto the open window | Drag one `.egscene` file from the file manager onto the open main window. | It opens like **File > Open Case Study...**. The application accepts exactly one local `.egscene` file. | All |
| R3 Unsaved changes | Run it after step 9, when the open case is the working copy and not the downloaded bundle. Change a scenario value and choose **File > Open Case Study...**. In the file dialog choose another copy of a bundle, never the downloaded bundle. The **Unsaved Scene** message follows the choice of a file. Then repeat with **File > Quit** (on macOS, **Quit** in the application menu). Make the change again before each repeat, and reopen the working copy after **Discard**. | The message offers **Save**, **Discard** and **Cancel**. Answer **Cancel** first and record that the edit and the open case stay. For the open request the status bar then shows `Open canceled. Current scene retained.` Then answer **Save** and **Discard** once each, and record what happened to the working copy. Afterwards the downloaded bundle must still equal its copy. | All |
| R4 Damaged file | Rename a plain text file to `something.egscene` and open it with **File > Open Case Study...**. | Record the message title and text, and whether the case that was open before is still shown. The application must not crash and must not change the file. | All |
| R5 Non-ASCII path | Put a copy of a bundle in a folder whose name and file name contain accented letters (for example é or ü) and one character outside the Latin alphabet. Open it, save a working copy into such a folder, and reopen the copy. | The bundle opens, the copy is saved, and the copy reopens. Record what appears. | All |
| R6 Assignment bundle | Open `Assignment_Gvc_Gdg_Ut.egscene`. | Its current data loads. Known station and rolling-stock fidelity gaps belong to #182, #228, and #181; they are not packaging failures. | All |
| R7 First start | Launch as in step 1 on an account whose update question has never been answered, and close the case chooser. | A question **Automatically Check for EGTRAIN Updates?** appears once, after the case chooser has been closed, with the buttons **Check Automatically** and **Don't Check Automatically**. Record the choice. **Help** then shows **Check for Updates...** and **Automatically Check for Updates**. | All |
| R8 Manual update check | Choose **Help > Check for Updates...**. | The message reads `EGTRAIN <version> is up to date.`, or a dialog titled `EGTRAIN <version> is Available` offers a newer stable release with the buttons **Update and Restart** (only where the installation can update itself), **Open Release Page**, **Later** and **Stop Checking**. A check that fails shows a warning titled `Check for Updates`. Record the text. | All |
| R9 In-app update | Run it last on a platform. Meet the precondition below, then choose **Update and Restart**. | The application restarts and **About EGTRAIN** shows the newer version. The hidden folder `.qegtrain-update-*` next to the installation is gone. On Windows the update helper cannot delete itself, so the application removes the rest about 20 seconds after it started. One backup of the previous installation, named like the installation plus `.egtrain-old`, stays next to it until the next update. If the dialog says the installation is not writable although the folder is writable, record the path and the text. | All |
| R10 Installation that cannot be updated in place | First confirm the precondition of R9 in a writable folder. Then place the installation where the folder that holds it cannot be written by the test account. That folder is the parent of `QEGTRAIN.app`, of the Windows application folder or of the AppImage. Choose **Help > Check for Updates...** again. | The update dialog has no **Update and Restart** button. Its text says `The packaged installation is not writable. Use the release page to update manually.` | All |

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
- Mark R9 or R10 not applicable, with a reason for this case, when no newer
  stable release is offered, when no older stable release that has the update
  entries can be installed for R9, or when the account cannot be denied write
  access for R10.

## Not available yet

The release does not build these today, so they have no subject.

| Check | What the release has today | Status |
| --- | --- | --- |
| Windows installer: fresh install, upgrade, uninstall, reinstall | The only Windows asset is `QEGTRAIN-windows-x64.zip` (#401). | No row until the release provides it |
| Windows portable archive under its own name | The zip is named `QEGTRAIN-windows-x64.zip` (#401). | No row until the release provides it |
| macOS disk image: open, copy to Applications, launch, run from a second folder | A zip of the folder `QEGTRAIN-Lebanon` (#403). | No row until the release provides it |
| macOS Developer ID signing and notarization: first open without a system warning | An ad hoc signature only (#404). | No row until the release provides it |
| Windows binary signature | None (#402). | No row until the release provides it |
| Open a bundle from the file manager or the command line, with the application closed and with it running | **File > Open Case Study...**, a drop onto the window and the `--scene` option (#416). | No row until the release provides it |
| Update of an installer-managed copy as against a portable copy | Only the portable packages exist, so R9 and R10 are the only update rows (#405). | No row until the release provides it |
| Message after a rolled-back update | The update helper restores the previous installation and starts it without a message (#527). | No row until the release provides it |

These rows are not part of the gate. They are added to the table of the record
when the release provides the asset.

## Result record

Copy [the template](release-rehearsal-template.md) to
`docs/development/release-rehearsal-<date>.md`, with the date of the rehearsal,
and fill it. Link every failed row to one focused defect issue. The record of
[2026-08-23](release-rehearsal-2026-08-23.md) is an earlier filled record of a
macOS rehearsal of an older candidate.

The release gate passes only after every required row passes on all three
platforms or is marked not applicable with a case-specific reason. Open one
focused defect for each failed root cause instead of adding implementation work
to issue #74.
