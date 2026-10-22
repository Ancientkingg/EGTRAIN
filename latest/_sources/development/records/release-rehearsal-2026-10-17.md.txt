# Release rehearsal 2026-10-17

Date: 2026-10-16 and 2026-10-17

This record names one candidate and the later candidates that rechecked its
defects. It follows the [release testing checklist](../release-testing-checklist.md)
and fills the [template](../release-rehearsal-template.md).

No release was published, so the rows that need a published release are left to
the owner. [Left to the owner](#left-to-the-owner) lists them.

## Candidate

The main rehearsal used one run of the `Release` workflow on all three
platforms. Later runs of the same workflow rechecked the defects that the main
rehearsal found. A row names the run that showed its result.

| Item | Value |
| --- | --- |
| Main candidate | [Release run 38954910966](https://github.com/Ancientkingg/EGTRAIN/actions/runs/38954910966), manual run of 2026-10-16, commit `5868098` on main |
| Recheck candidate 1 | [Release run 39019990227](https://github.com/Ancientkingg/EGTRAIN/actions/runs/39019990227), manual run of 2026-10-17, commit `950b145` |
| Recheck candidate 2 | [Release run 39032467694](https://github.com/Ancientkingg/EGTRAIN/actions/runs/39032467694), manual run of 2026-10-17, commit `b6e2da8` |
| Recheck candidate 3 | [Release run 39049264770](https://github.com/Ancientkingg/EGTRAIN/actions/runs/39049264770), manual run of 2026-10-17, commit `2c8f41c` |
| Version shown by About EGTRAIN, macOS | 1.0.0, the CMake baseline (main candidate) |
| Version shown by About EGTRAIN, Windows | 1.0.0, the CMake baseline (all four candidates) |
| Version shown by About EGTRAIN, Linux | 1.0.0, read by the Rehearse Linux job (all four candidates) |

In the main candidate all package jobs and the Rehearse Linux job passed. The
Sign Windows jobs and the release job were skipped, as they are for a manual
run. In recheck candidate 2 the Sanitizers job failed. The package jobs and the
Rehearse Linux job passed, and the files of that run are the ones that were
rechecked. Recheck candidate 3 holds [#886](https://github.com/Ancientkingg/EGTRAIN/pull/886) and
the fixes for the two tests that failed in the Sanitizers job; Windows row R9
was rechecked on it.

An earlier Windows pass on an older run is not part of this record.

## Assets

Every file of a candidate came from the artifacts of one run. The Windows and
macOS files carried the download mark of a browser. The exceptions are in the
last rows.

| Platform and asset | Exact file name | Downloaded from |
| --- | --- | --- |
| macOS package | `EGTRAIN-1.0.0-macOS-arm64.dmg`, 16913099 bytes | Artifact `QEGTRAIN-macos-arm64` of run 38954910966. Run 39032467694 gave the same name; its size was not recorded. |
| Windows installer | `EGTRAIN-Setup-1.0.0-x64.exe`, 11886189 bytes (run 38954910966) and 11989515 bytes (run 39019990227) | Artifact `EGTRAIN-windows-x64-installer`. Run 39032467694 gave the same name; its size was not recorded. |
| Windows portable archive | `EGTRAIN-Portable-1.0.0-x64.zip`, 14023187 bytes (run 38954910966) and 14189332 bytes (run 39019990227) | Artifact `EGTRAIN-windows-x64-portable` |
| Older Windows installer (row W5) | `EGTRAIN-Setup-0.9.0-x64.exe`, 11990379 bytes | No older Setup exists in any release. It was compiled on the test machine from the package files of run 39019990227 with only the version changed. It is not a release asset. |
| Linux package | `QEGTRAIN-linux-x86_64.AppImage`, from the artifact `QEGTRAIN-linux-x86_64` (34957906 bytes in its outer zip for run 38954910966). The size of the AppImage itself was not recorded. | The Rehearse Linux job downloads the artifact that the Package Linux job uploaded. |
| Assignment bundle | `Assignment_Gvc_Gdg_Ut.egscene`, 4750 bytes | Artifact `EGTRAIN-scenes` |
| Bundle used for the workflow | `Paimpol.egscene`, 18562 bytes on macOS and Windows. The other six bundles were downloaded with it: Amsterdam_Hilversum_Student 98298, Copenhagen 131539, Lebanon 26725, Milano_Brescia 32897, Netherlands 98471 bytes. | Artifact `EGTRAIN-scenes`. The Linux job packs the Paimpol scene of the AppImage with the `scene_tool` of the image, so that bundle is not a download. |
| Bundle of the previous release (row R12) | `Paimpol.egscene` of v1.0.2, 18424 bytes | The assets of release v1.0.2 |
| Package of the newest stable release (row R9) | v1.0.2: `QEGTRAIN-macos-arm64.zip`, 20425836 bytes, and `QEGTRAIN-windows-x64.zip`, 43995917 bytes | The update offered by the candidate itself |
| `update-manifest.json` (published release only) | Not applicable: a run has no manifest | |

## Environment

| Item | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Operating system and exact version | macOS 26.6.2 | Windows 11 Pro, build 26300 | Ubuntu 24.04 on a GitHub hosted runner |
| Processor architecture | Apple silicon, arm64 | ARM64. The x64 program ran under the emulation of Windows. | x86_64 |
| Machine type | A macOS virtual machine with 4 processors, 4 GB of memory and a 1280x800 screen | A Windows 11 virtual machine with 4 cores, 4 GB of memory, a 1024x768 screen and no graphics card. There was no physical x64 PC, so timings and drawing speed do not stand for one. | The Rehearse Linux job, with a virtual display, a D-Bus session and the accessibility bus. It is not a desktop session. |
| User rights | The account is an administrator. | A standard account without administrator rights. The administrator account was signed out for the GUI steps. | The runner user |
| Qt, CMake or other development tools present on the machine | None in the virtual machine. The Homebrew folders `opt` and `Cellar` were renamed away for the test, and development copies of the app were unregistered from Launch Services. | None. For the older Setup of row W5 only, Inno Setup was installed after the main rehearsal. | The job installs the display, the accessibility bus and test tools, and no Qt package |
| Visual C++ runtime installed before the test | Not applicable | Only the Arm64 runtime. The package holds the x64 runtime DLLs next to `QEGTRAIN.exe`. | Not applicable |
| Anything installed or changed to make the package start | Nothing. To go on after M4, the download mark of the copy in Applications was removed with `xattr` (see M4). Gatekeeper, privacy settings and System Integrity Protection were not changed. | Nothing | Nothing |
| Dialogs shown at first start, in order | None for the disk image. Then the refusal of M4. After the download mark was removed: Open a Case, then the update question, which on this candidate showed no title and no question as its main text (#840). At the first File > Open Case Study: three questions about Desktop, Documents and Downloads (#841). | Setup: the SmartScreen dialog, then Setup pages License Agreement, Select Destination Location, Select Additional Tasks, Ready to Install and Completing. Application: Open a Case, then Automatically Check for EGTRAIN Updates?. | Open a Case, then the update question, answered by the script |
| System warning at first start, with its text | `"QEGTRAIN" Not Opened`: `Apple could not verify "QEGTRAIN" is free of malware that may harm your Mac or compromise your privacy.` | Setup: `Windows protected your PC`: `Microsoft Defender SmartScreen prevented an unrecognized app from starting. Running this app might put your PC at risk.` After More info: `Publisher: Unknown publisher` and the button Run anyway. `QEGTRAIN.exe` of the installed and the portable copy started with no warning. | None |

## Results

A result is PASS, FAIL, NOT APPLICABLE with a reason, or NOT RUN. A date is
written month-day and belongs to 2026. A cell without a date has the date of the
run named in the note.

The Windows steps 1 to 9 ran on the installed copy (row W2) and again on the
portable copy (row W6). The R rows ran on the portable copy, except R7, which
was seen in row W2. The macOS rows ran on the copy in Applications. The Linux
steps ran in the Rehearse Linux job of the three runs, all with the same result.

| Check | macOS | Windows | Linux | Evidence or defect |
| --- | --- | --- | --- | --- |
| Clean GUI launch | PASS 10-17 | PASS 10-16 | PASS 10-16 | Windows: both copies, no warning for the program |
| Open downloaded `.egscene` | PASS 10-17 | PASS 10-16 | PASS 10-16 | Paimpol opened as `Scene loaded: Paimpol (2 services, 15 routes)` |
| Loaded Data and validation | PASS 10-17 | PASS 10-16 | PASS 10-16 | Loaded Data lists the categories. The Case Study, Infrastructure and Passengers rows show Warning on the candidate's Paimpol. |
| Scenario select or edit | PASS 10-17 | PASS 10-16 | PASS 10-16 | The run review names Paimpol / baseline |
| Save As outside package | PASS 10-17 | PASS 10-16 | PASS 10-16 | The dialog proposes the downloaded bundle itself, as the checklist warns |
| Source bundle unchanged | PASS 10-17 | PASS 10-16 | PASS 10-16 | See the section Source bundle |
| GUI simulation completes | PASS 10-17 | PASS 10-16 | PASS 10-16 | Run Results shows Status: Completed |
| Result views open | PASS 10-17, timetable table only | PASS 10-16 | PASS 10-16 | Windows and Linux opened the timetable, delay, speed and blocking-time views. macOS opened the timetable table only. |
| CSV export | PASS 10-17 | PASS 10-16 | PASS 10-16 | Each export has a provenance file next to it |
| PNG export | PASS 10-17 | PASS 10-16 | PASS 10-16 | |
| Saved copy reopens | PASS 10-17 | PASS 10-16 | PASS 10-16 | The saved horizon edit was still in the copy |
| No source or development dependency | PASS 10-17 | PASS 10-16 | PASS 10-16 | macOS: 39 Mach-O files checked, every dependency is a system library or inside the app, the app ran with Homebrew renamed away. Two build machine paths remained in the bundle (#842, fixed). |
| R1 Run from a second location | PASS 10-17 | PASS 10-16 | NOT RUN | The job does not cover this row |
| R2 Drop a bundle onto the open window | PASS 10-17, one file | PASS 10-16, one file | NOT RUN | A drop of several files was not tried on any platform. The job does not cover this row. |
| R3 Unsaved changes | PASS 10-17 | PASS 10-16 | NOT RUN | Cancel, Save and Discard behave as described. macOS draws the message without a title and names the buttons Save, Cancel and Don't Save, as the checklist says. |
| R4 Damaged file | PASS 10-17 | PASS 10-16 | NOT RUN | The open case stayed and the file was unchanged. The two Windows messages differed (#828, fixed). |
| R5 Non-ASCII path | PASS 10-17 | PASS 10-16 | NOT RUN | Opened, saved a copy and reopened it |
| R6 Assignment bundle | PASS 10-17 | PASS 10-16 | NOT RUN | `Scene loaded: Assignment Gvc-Gdg-Ut (4 services, 2 routes)` |
| R7 First start | PASS 10-17 | PASS 10-16 | NOT RUN | The macOS question lacked its main text (#840), seen fixed in run 39032467694. Windows: seen in row W2. The job answers the question but does not record its text. |
| R8 Manual update check | PASS 10-17 | PASS 10-16 | NOT RUN | macOS: no title and no offered version in the main text (#840), seen fixed in run 39032467694. Windows: the release notes ran together (#828), seen fixed in run 39032467694 after #873. |
| R9 In-app update | PASS 10-17, second attempt | PASS 10-17, run 39049264770 | NOT RUN | macOS: the first attempt ended with `The update download timed out.` on the network of the virtual machine. The second attempt installed 1.0.2 and left `QEGTRAIN.app.egtrain-old`. Windows: failed on the earlier candidates ([#826](https://github.com/Ancientkingg/EGTRAIN/issues/826)); on run 39049264770 the portable copy installed 1.0.2 at the first attempt and kept `EGTRAIN.egtrain-old`. See the section Defects. |
| R10 Installation that cannot be updated in place | PASS 10-17 | PASS 10-16 | NOT RUN | Both texts of the checklist were seen, also for a copy that runs from a disk image on macOS |
| R11 Save an installed example | PASS 10-17 | PASS 10-16 | NOT RUN | Windows: two dialogs started in the installation folder ([#827](https://github.com/Ancientkingg/EGTRAIN/issues/827)), seen fixed in run 39019990227. The installed folder was unchanged. macOS: a single file inside an installed example folder was not tried. |
| R12 Bundle of the previous release | PASS 10-17 | PASS 10-16 | NOT RUN | The Paimpol bundle of v1.0.2 opened without a version message and ran. It reports that 140 of 140 route sections have no signalling level, which is a property of the old bundle. |
| M1 File type in Finder | PASS 10-17 | NOT APPLICABLE, macOS only | NOT APPLICABLE, macOS only | Kind `EGTRAIN Case Study`, opens with `QEGTRAIN`. The `.zip` file keeps Archive Utility. |
| M2 Open a case study by double-click | PASS 10-17 | NOT APPLICABLE, macOS only | NOT APPLICABLE, macOS only | One process, no second window. The question has no title on macOS. |
| M3 Double-click a damaged file and a special path | PASS 10-17 | NOT APPLICABLE, macOS only | NOT APPLICABLE, macOS only | The damaged file was not listed under Recent Scenes. The special path opened each time. |
| M4 First start of an ad hoc build | NOT RUN, seen up to the password prompt | NOT APPLICABLE, macOS only | NOT APPLICABLE, macOS only | See the notes below the table. The rest is left to the owner. |
| M5 First start of a notarized build | NOT APPLICABLE, the app is ad hoc signed (`Signature=adhoc`) | NOT APPLICABLE, macOS only | NOT APPLICABLE, macOS only | |
| M6 Folder access questions | PASS 10-17, run 39032467694, without the New Case Study step | NOT APPLICABLE, macOS only | NOT APPLICABLE, macOS only | The three questions appeared at the first File > Open Case Study in the main candidate ([#841](https://github.com/Ancientkingg/EGTRAIN/issues/841)). On run 39032467694, started from the Applications folder of a fresh virtual machine, File > Open Case Study... and File > Open Scene Folder... opened in the `Scenes` folder of the app, in column view, with no question. The New Case Study variant was not run. A copy started from a folder inside the home folder asked the three questions at once: its column view lists the home folder, as the expected result of the last step describes. |
| W1 Windows assets | NOT APPLICABLE, Windows only | PASS 10-16, run page only | NOT APPLICABLE, Windows only | Both names carry 1.0.0. A run has no release page and no manifest. The release page row is left to the owner. |
| W2 Install | NOT APPLICABLE, Windows only | PASS 10-16 | NOT APPLICABLE, Windows only | No administrator prompt. One entry `EGTRAIN`, version 1.0.0, no desktop shortcut, nothing below Program Files. Recheck on run 39019990227 passed. |
| W3 Uninstall | NOT APPLICABLE, Windows only | PASS 10-16 | NOT APPLICABLE, Windows only | The working copy, exports and run output stayed. A `.egscene` file no longer opened in EGTRAIN. Recheck on run 39019990227 passed. |
| W4 Reinstall | NOT APPLICABLE, Windows only | PASS 10-16 | NOT APPLICABLE, Windows only | The shortcut started EGTRAIN. The question of R7 did not return. |
| W5 Upgrade | NOT APPLICABLE, Windows only | PASS 10-17, with a substitute Setup | NOT APPLICABLE, Windows only | Not run on the main candidate because no older Setup exists. Rechecked on run 39019990227 with the Setup built for version 0.9.0. One entry, one Start Menu entry, one folder, settings kept. About EGTRAIN showed 1.0.0 because that Setup holds the same program files. |
| W6 Portable archive | NOT APPLICABLE, Windows only | PASS 10-16 | NOT APPLICABLE, Windows only | No installed-apps entry, no Start Menu entry, no marker file, no uninstaller. The extracted program kept its download mark and started with no warning. |
| W7 Open a case study by double-click | NOT APPLICABLE, Windows only | PASS 10-16 | NOT APPLICABLE, Windows only | The answer Save was not tried |
| W8 In-app update of an installed copy | NOT APPLICABLE, Windows only | NOT RUN | NOT APPLICABLE, Windows only | No older Setup exists and no release lists the installer in its manifest. Left to the owner. |
| W9 Double-click a damaged file and a special path | NOT APPLICABLE, Windows only | PASS 10-16 | NOT APPLICABLE, Windows only | The two messages had different titles and text ([#828](https://github.com/Ancientkingg/EGTRAIN/issues/828)). Run 39019990227 showed one title and one text. |

M4 on macOS, in the order that the checklist gives. The disk image window showed
an icon view with large icons, without toolbar, sidebar, path bar and status bar,
and `QEGTRAIN` on the left and `Applications` on the right at the same height.
The first start was refused with `"QEGTRAIN" Not Opened`, with the buttons Move to
Trash and Done and no Open button. Privacy & Security showed `"QEGTRAIN" was
blocked to protect your Mac.` with Open Anyway. Open Anyway showed `Open
"QEGTRAIN"?` with Move to Trash, Open Anyway and Done. Its Open Anyway asked for
the name and password of an administrator. The password was not entered, because
the test needs a person to enter it. Whether EGTRAIN starts after the password,
and which dialog comes next, was not seen. The texts and the order matched the
checklist. While the password prompt was open, a `QEGTRAIN` process existed but
showed no window. To run the other rows, the download mark of the copy in
Applications was removed with `xattr` afterwards.

## Source bundle

File: `Paimpol.egscene`, 18562 bytes on macOS and Windows.

| Item | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Comparison command used | `cmp` against a reference copy | `fc /b` in a command prompt, against a reference copy | The script compares the bundle with a byte copy |
| Result of the comparison | No difference | No differences encountered | Equal |
| Modification time before | 2026-10-16 15:47:31 on the clock of the virtual machine | Not recorded for this candidate | Noted by the script |
| Modification time after | The same | Not recorded for this candidate | The same, as the script checks |

## Defects

Each failed row, and each other difference that the rehearsal found, has one
issue. All of them are closed. A fix after a failed row needs a new candidate, so
the last column names the candidate that showed the fix.

| Platform | Row | Issue link | Fix and where it was seen |
| --- | --- | --- | --- |
| Windows | R9 | [#826](https://github.com/Ancientkingg/EGTRAIN/issues/826) | The update restarted the old version and said nothing. Run 39019990227 shows the notice that the update could not be applied or was rolled back. The update itself still failed there. Run 39049264770 installed 1.0.2. See below. |
| Windows | W2 step 8, R11 | [#827](https://github.com/Ancientkingg/EGTRAIN/issues/827) | Fixed. Run 39019990227: Export Data and Save Scene As Folder start in Documents, and the installed destination is still refused. |
| Windows | W9, R8 | [#828](https://github.com/Ancientkingg/EGTRAIN/issues/828) | The damaged-file message is the same on both paths in run 39019990227. The release notes kept their list structure only after [#873](https://github.com/Ancientkingg/EGTRAIN/pull/873), seen in run 39032467694. |
| macOS | R7, R8, R3, M2 | [#840](https://github.com/Ancientkingg/EGTRAIN/issues/840) | Fixed for R7 and R8 and seen in run 39032467694: the main text now holds the question and the offered version. |
| macOS | Step 1, M6 | [#841](https://github.com/Ancientkingg/EGTRAIN/issues/841) | Fixed in run 39032467694 and seen on a fresh virtual machine with the app in Applications (row M6). |
| macOS | No source or development dependency | [#842](https://github.com/Ancientkingg/EGTRAIN/issues/842) | Fixed. Run 39032467694: no path under `/opt/homebrew` or `/usr/local` in 39 Mach-O files. |

On Windows R9 the cause was the test machine and not the update helper, as far as
it could be told. The program of release 1.0.2 fails to start from a staged
folder on this machine with the error `Insufficient system resources exist to
complete the requested service`, and it uses about 2 GB of the 4 GB. After a
restart of the machine, the same update installed 1.0.2 and left the backup
`.egtrain-old`. Staging also runs a PowerShell unpack with a limit of 60 seconds,
which timed out once, and a failed attempt left a hidden `.qegtrain-update-*`
folder behind. [#886](https://github.com/Ancientkingg/EGTRAIN/pull/886) gives
the unpacking more time and clears failed staging folders. It was merged after
run 39032467694. On run 39049264770, which holds it, the portable copy offered
`EGTRAIN 1.0.2 is Available`, prepared the update in about 21 seconds, said
`The update is ready. EGTRAIN will restart now.`, and restarted into 1.0.2 at the
first attempt, with the backup `EGTRAIN.egtrain-old` next to it. A hidden
`.qegtrain-update-*` folder holding only the update helper stayed behind: the
program that removes it after a restart is the new one, and 1.0.2 predates that
cleanup, so this row can show the cleanup only once a newer release is the target.

Other fixes were seen in run 39032467694 on Windows: the clipped text of default
buttons ([#874](https://github.com/Ancientkingg/EGTRAIN/pull/874)) and long case
names in the case chooser
([#877](https://github.com/Ancientkingg/EGTRAIN/pull/877)). In the case chooser,
Enter on an unavailable recent case opened another case. That is fixed by
[#887](https://github.com/Ancientkingg/EGTRAIN/pull/887), merged after the last
candidate.

One gap is still open: [#883](https://github.com/Ancientkingg/EGTRAIN/issues/883).
At 1024 by 768 the toolbar separators change between thin lines and grey blocks
with the window width, the zoom buttons drop off the toolbar below about 900
pixels, and the restored window is 1044 by 788, larger than the screen.

## Not exercised

| What was not run | Platform | Why |
| --- | --- | --- |
| Rows R1 to R12 | Linux | The Rehearse Linux job covers the steps 1 to 9 only. The AppImage of run 39049264770 was also started by hand in an x86_64 Ubuntu 24.04 container: headless runs of Paimpol and Copenhagen ended normally and a window start under a virtual display ran 20 seconds without errors. A minimal system needed `libgl1`, `libharfbuzz0b` and `libfontconfig1`. |
| Dragging a bundle from a file manager (R2), the questions of a desktop environment at first start, the file dialog of a desktop environment | Linux | The job uses a virtual display and the dialog of Qt |
| Drop of several files (R2) | macOS, Windows | No multi-file selection was made on macOS. It was not tried on Windows. |
| Delay, speed and blocking-time views (step 7) | macOS | Only the timetable table was opened |
| Answer Save in row W7 | Windows | Not tried |
| A single file inside an installed example folder (R11) | macOS | Not tried |
| M4 after Open Anyway | macOS | Needs an administrator password, see below |
| M5 | macOS | The candidate is ad hoc signed |
| W8 | Windows | No older Setup that updates through the installer, and no release that lists the installer |
| Real Setup of an older release for W5 | Windows | None exists. A Setup built from the candidate stood in. |
| A physical x64 PC | Windows | The machine was an ARM64 virtual machine under emulation. Timings, drawing speed and the memory limit of R9 do not stand for one. |
| Graphics card behaviour | Windows | The machine has no graphics card. Drawing showed no fault, but speed was not measured. |
| Trackpad pinch and scroll | macOS, Windows | The test machines have no trackpad |
| The source build on Linux | Linux | Not part of the packaged rehearsal. A build from a clean Ubuntu 24.04 container of arm64 on 2026-10-17 followed the developer guide and passed all 314 tests, after [#875](https://github.com/Ancientkingg/EGTRAIN/pull/875) fixed the guide and `test_measure_peak_rss` for containers without an init process. |

## Left to the owner

These need a published release, a person at the keyboard or a different machine.

- The published release. Row W1 on the release page and the manifest: the entry
  `windows-x64` must name the archive, `windows-x64-installer` must name the
  installer, and the `size` of each must equal the downloaded file.
- Row W8, the in-app update of an installed Windows copy. It needs a published
  release with the installer in its manifest and an older installed Setup.
- The update test across two published releases, tracked in
  [#405](https://github.com/Ancientkingg/EGTRAIN/issues/405).
- Row M4 after Open Anyway on macOS: enter the administrator password, then
  record whether EGTRAIN starts and which dialog, if any, comes next.
- Row R9 on a physical x64 Windows PC. The virtual machine passed it on run
  39049264770.
- Row W5 with a real older Setup once one is published.

Issue [#74](https://github.com/Ancientkingg/EGTRAIN/issues/74) stays open until
the owner has decided whether this record, with the rows above, is enough.
