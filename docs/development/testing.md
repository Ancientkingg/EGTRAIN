# Testing

This page covers the CTest suite: how to run it, its labels, the tests each platform registers and the
package checks. It ends with the checks to run for each kind of change and the documentation site.

## Unit Tests

```bash
ctest --test-dir build --output-on-failure
```

With a multi-config generator such as Visual Studio, build and test the same
configuration, for example `cmake --build build --config Release` and
`ctest --test-dir build -C Release`. `ctest -N` lists the tests registered on
the current platform.

Tests can run side by side: `ctest --test-dir build -j N --output-on-failure`.
A test that starts the application keeps its user settings in a folder of its
own, most of them below `build/settings`, so none of them reads or changes the
settings of the user. CI runs `ctest --parallel` with the number of cores of
the runner.

Scene compatibility tests cover manifest probing, independent schema/bundle
classification, hostile newer bundles, and transactional test-only migration
chains. They also cover the validation of an upgraded copy, a source scene that
cannot be loaded, and a refused write. The refused-write case skips itself when
a new directory can still be created in a read-only one, for example for a
process with root rights. The production migration registry is empty on
purpose: every scene written so far has version number 1, so there is no
number to convert from.
`test_sceneversions` reads the scenes in `tests/fixtures/scene-versions`. They
are byte copies of scenes written by older builds, listed with their origin in
`manifest.json`, and a fixture is never edited or saved again. For each one the
test checks the class, the version numbers, loading, validation, the sizes of
the loaded model, and that a copy upgrade is refused and leaves the fixture
unchanged. The stored draft-layout scene is pinned as unsupported with the
diagnostic that names the draft layout, and as not runnable. A rejected schema
number is checked on a copy that the test makes, with the diagnostic that names
it. A guard fails when a version constant is raised without a fixture of the
previous number and either a migration for it or manifest entries that list
every fixture of that number as unsupported.

A release that raises `kCurrentSceneSchemaVersion` or `kCurrentSceneBundleVersion`
changes the scene format. After it is published, download one of its bundles from
the release and copy it unchanged into `tests/fixtures/scene-versions`. Add an
entry to `manifest.json` with the release in `origin` and the other keys of an
existing entry (`counts` come from the loaded model). If the bundle is meant to
run, add a `test_scene_version_run_*` test next to the existing ones in the root
`CMakeLists.txt`, with the same `TIMEOUT`, and add its name to the `integration`
list of `egtrain_label_tests`. The next raise of a number then finds a fixture of
the current one.

`scene_tool` has no `migrate` command; it offers `import`, `pack`, `unpack`,
`export` and `validate`. The
[Compatibility boundary](../architecture/scene-model.md#compatibility-boundary)
says which scenes are supported.

The native builders and TrackPreview tests operate on an in-memory canonical
`SceneModel`; the builders perform no input-file reads. GUI and headless runs
both enter the same `DispatchController::prepareScene` path.

`test_scenehistory` checks `SceneHistory` without Qt: the order of Undo and
Redo, the Redo branch that an edit discards, the saved position across undo,
redo, edits and dropped snapshots, both limits with injected values, the labels,
and the replacement of the scene by `reset`. It also removes each part of a rich
scene in turn and checks that `estimateSceneModelBytes` drops, so a container
that the estimate leaves out fails the test. For each committed scene it records
101 edits with the default limits and checks that the step count and the estimate
stay within them.

`test_scenedocument` checks `SceneDocument` without a window: the signals, the
revision and the unsaved state of `replace`, `markChanged`, `markSaved` and
`restore`, and that `assignAuthoredScene` takes every authored part of a
snapshot and keeps the loaded data, the source files and the version that saved
the scene.

Each panel of `editors/` has a test, `test_editor_<panel>`, such as `test_editor_casesettings` and
`test_editor_service` (labels `unit` and `gui`, run with the `offscreen` platform). It puts the panel over a
`SceneDocument` and over a `UserPrompts` that records its questions, so that no dialog opens, and works the widgets of
the panel. It checks that a refresh lists the model and, as far as the panel has them, that an edit, an addition and a
deletion change the model and mark the document once, that a refused or cancelled edit changes nothing, and that a rename
keeps what refers to the entity.
The infrastructure facets have one test for each file of facets: `test_editor_infrastructure_topology`,
`test_editor_infrastructure_blocks`, `test_editor_infrastructure_stations` and `test_editor_infrastructure_signals`.
`test_editor_infrastructure` checks the panel itself with facets of its own, including where its buttons and the sections of a route are placed. `test_infrastructureguidance` checks the sentence above the table for each of the thirteen facets, with and without entities and a scene.

`test_recoverystore` checks `RecoveryStore` in one process: the layout of an
instance folder, writing and replacing a copy, a failed write that keeps the
previous copy, the copies that an ended instance left behind, the choice of the
newest complete bundle, unusable and unknown records, pruning, the limit of 20
kept instances, clear and close, adopt and discard, and the comparison of a
source with its copy. A case that needs POSIX permissions skips itself on
Windows and when the process may write into a read-only folder anyway.
`test_recoverystore_process` (label `integration`) starts the test program again
as a child that writes a copy and holds its lock, and as a child that ends
without a clean close. The parent checks that a scan lists nothing for the
running child, that it lists the copy once the child was killed or has ended,
and that a new store can adopt that copy.

`test_recoverycontroller` runs `RecoveryController` with an event loop and an
interval of 10 ms, which it sets in `QEGTRAIN_RECOVERY_INTERVAL_MS`; the default
is 30 s. It checks the interval variable, that the edits of one interval give
one write of the newest scene, that later edits do not move the timer, that a
revision on disk is not written again, that edits during a write give one more
write, that a save, a replacement or a return to the clean state leaves no copy
when a write is running or waiting, that shutdown waits for the write and
removes the instance folder, that the destructor leaves the copy, that a failed
write or removal is reported once per run, and the scan, adopt and discard
calls, including an adoption that fails after the removal of the previous copy
was handed to the worker. The test holds the worker thread at two points of a
job with a hook that the constructor takes, so the cases for a write in flight
do not depend on how fast the write is. The checks that nothing happens wait a
fixed 100 to 150 ms, and the timer case uses an interval of 2 s. The removal
case changes the permissions of a folder and is skipped where that has no
effect, as for a superuser and on Windows.

`test_recoveryprompt` (labels `unit` and `gui`) shows `RecoveryPrompt` on the
offscreen platform and never calls `exec()`. It checks one row for each copy
with its name, time and source, the note for a changed, missing, newer or
unknown original and for a copy that this version cannot open, the default
button of each row, the results of Recover, Decide Later, Escape, closing the
window and Enter, that Discard needs a second click that names the case, the
line about copies removed over the limit, and that a long name or path, many
rows and a large font keep the dialog inside the available area with the
buttons in reach.

`test_openrequestrelay` (label `integration`) checks `OpenRequestRelay`, which
hands a file from a second launch to the running window. The test program is
the running instance, and it starts itself again as the second process. It
checks that a path with spaces and non-ASCII characters arrives once and
unchanged and that the second process ends with 0, that a request for another
application folder is refused, that a name held by a running instance is not
taken over while a name left behind by a killed one is, that a request in two
parts is accepted and bytes that are no request, or more than a request holds,
are dropped, that only the newest postponed request comes back, and the rule for
the test variables. The relay also takes the `QFileOpenEvent` that macOS sends
when Finder opens a file with the program. The test sends such events to the
application: the path of a file with spaces and non-ASCII characters is reported
once and unchanged from the event loop, an event for a URL that is no local file
is left alone, of two events in one turn the newest remains, and a request that
nothing is connected to yet waits for a receiver. On macOS and Linux the socket
files lie in a folder directly below `/tmp`, because a socket path is limited to
about 100 characters.
`test_startup_launch_contract` adds the window: a window started with a file,
and one with the chooser open, each open a file that a launch without test
variables hands to them, the window leaves a handed file that does not load out
of the recent cases, and a scripted launch with a file keeps its own window. A
run with any `QEGTRAIN_E2E_` variable neither listens nor hands over a file,
unless `QEGTRAIN_E2E_OPEN_RELAY` is set as well, which the contract sets for the
window only. The test hook `QEGTRAIN_E2E_OPEN_REQUEST` names a file that the run
hands to the application as a `QFileOpenEvent`, as the first show of the window
begins (`QEGTRAIN_E2E_OPEN_REQUEST_AT=launch`, the default), 50 ms later while the
program still starts (`startup`), from inside the open case chooser (`chooser`) or
one second later (`running`). It prints whether the chooser is open when the event
comes, and the chooser and the status line three seconds after it. The contract
uses it to show that a window started without a file opens a file from an open
event, with spaces or non-ASCII characters in its name, without the chooser, that a
running window opens one too, that an event from inside the open chooser closes the
chooser and opens the file, and that a malformed file is reported and stays out of
the recent cases. On macOS the startup callback waits 300 ms before it opens
the chooser, so that the event of a launch from Finder is seen first; the `startup`
moment checks that no chooser opens then. A real double-click in Finder is not part
of it.

### Test labels

Every test has exactly one of `unit` or `integration`. Configuration fails
when a test has neither or both.

- `unit`: runs in-process or is a static check. It starts no QEGTRAIN,
  `scene_tool`, update helper, or socket server.
- `integration`: starts or inspects a built program (`QEGTRAIN`, `scene_tool`,
  `egtrain_update_helper`, or a test that starts its own executable as a child)
  or talks to a loopback server.
- `gui`: needs a Qt platform plugin (the tests use `offscreen`) or launches
  QEGTRAIN in GUI mode. Combine it with either of the labels above.
- `slow`: takes more than 20 seconds on a development machine.
- `network`: sends a request to a server on the internet. It is registered only
  with `-DEGTRAIN_NETWORK_TESTS=ON`, so no other test run needs the internet.
  Combine it with `integration`. See [HTTPS update check](#https-update-check).

To add a test and register its labels, see [Coding guidelines](coding-guidelines.md#tests).

```bash
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L unit -LE gui --output-on-failure
ctest --test-dir build -L unit -LE slow --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -LE slow --output-on-failure
ctest --test-dir build -L gui --output-on-failure
```

On Windows add `-C Release` to each command.

Scene tests use CTest labels too:

```bash
ctest --test-dir build -L scene-v1 --output-on-failure
ctest --test-dir build -L legacy-compat --output-on-failure
ctest --test-dir build -LE legacy-compat --output-on-failure
```

`scene-v1` covers the canonical scene model. `legacy-compat` covers only the
explicit importer/exporter boundary; normal simulation is not in that label.
`scene-v2` covers `.egscene` container round-trips, hostile-archive checks and
the released scene fixtures.

`test_scenewriter` saves each committed scene and the `line` and `minimal`
fixtures, loads the result and saves it again, and requires the two saves to be
equal byte for byte. It never compares a save with the committed bytes, because
a committed file need not be in the writer's form.

The scene tests `test_scenebundle`, `test_scenewriter`, `test_scenecompatibility`,
`test_sceneimporter` and `test_sceneexporter` also give the scene API folder and file
names with Japanese and Thai characters, which no Windows code page holds together.
`test_scenebundle` also points the temporary folder at such a name.

Run the focused bundle test with:

```bash
ctest --test-dir build -L scene-v2 --output-on-failure
```

### Platform coverage

CMake prints one `Windows: skipping ...` status line at configure time for each
group of tests it leaves out. `yes` below means the test is registered on that
platform, and `partly` that it is registered and only some of its checks run there.

| Test | macOS | Linux | Windows | Reason |
| --- | --- | --- | --- | --- |
| `test_csv_export_smoke`, `test_creator_acceptance_smoke`, `test_dialog_presentation_contract` | yes | yes | no | Bash scripts that use `awk`, `mktemp` and `/dev/stderr`; Windows has no bash on `PATH` that can be relied on. |
| `test_editor_smoke` | yes | yes | no | A Bash script; Windows has no bash on `PATH` that can be relied on. |
| `test_recovery_smoke` | yes | yes | no | A Python script that ends the application with `_Exit` and starts it again. The lock files of the recovery copies are not checked after a crash on Windows, so the test is registered under `if(UNIX)` until they are. Two of its scenarios crash the application on purpose to check the crash note. |
| `test_crashnote` | yes | yes | partly | Starts copies of itself that crash on purpose and reads their crash notes. On Windows it runs the signal and exception cases on the main thread only, and the notes have no stack. Every child leaves a system crash report. |
| `test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`, `test_route_diagram_smoke` | yes | yes | no | The first three are Bash scripts, and Windows has no bash on `PATH` that can be relied on. `test_route_diagram_smoke` is registered with them under `if(UNIX)`, so Windows does not register it either. |
| `test_visual_polish_smoke_live`, `test_visual_polish_smoke_replay`, `test_visual_polish_smoke_replay_copenhagen`, `test_visual_polish_smoke_signals`, `test_visual_polish_smoke_stations` | yes | no | no | Phases of a Bash script, registered on macOS only. The script also runs on Linux with `QEGTRAIN_APP=$PWD/build/QEGTRAIN`. |
| `test_capture_screenshots` | yes | no | no | Runs the capture phase of the macOS app bundle. Disabled until the pictures of `docs/images` are taken again with Qt 6 in the current dialog style. |
| `test_case_chooser_contract` | yes | yes | no | Compares backslash paths with the forward-slash paths the application reports, writes them into a `QSettings` INI file where backslash is an escape, and creates a directory symlink. |
| `test_package_contents_smoke` | yes | no | no | Checks the `.app` bundle layout with macOS tools, and the `.egscene` document type that its `Info.plist` declares. |
| `test_measure_peak_rss` | yes | yes | no | Tests the parser, the commands and the process handling of the macOS `/usr/bin/time -l` collector. It does not need that tool. |
| `test_windows_image_size` | no | no | yes | Reads the PE header of `QEGTRAIN.exe`. |
| `test_windows_default_stack` | no | no | yes | Reads the PE header of `scene_tool.exe`. |
| `test_win32_configure_rejected` | no | no | yes | Configures for 32-bit Windows with the Visual Studio generator and expects the message that only x64 is supported. A Ninja build tree, such as the Windows CI leg, runs it with the default generator, which is Visual Studio on Windows. |
| `test_verify_signatures` | no | no | yes | Signs copies of `scene_tool.exe` with a throwaway self-signed certificate through `New-SelfSignedCertificate` and `Set-AuthenticodeSignature`, and runs `tools/release/verify_signatures.ps1` on them. It is not registered when no PowerShell is found. |
| `test_startup_launch_contract` | yes | yes | partly | The two pseudo-terminal launches, the two `--interactive` launches, the window launch with a bundle argument, the open events and the file launches while a window runs run only on macOS and Linux. A bundle with a non-ASCII file name is launched without a window on all three systems. |
| `test_updatepreparation` | partly | partly | partly | The checks that stage a disk image and a zip package run on macOS only and need `hdiutil`, `ditto`, `codesign` and `python3`. The AppImage check runs on Linux only. |
| `test_make_dmg` | yes | partly | partly | The image, mount and signature checks need `hdiutil`, `ditto` and `codesign`. Elsewhere the test checks the file name, the input rules, the message for a missing tool, the command lines and the handling of a failing or timed-out command. |
| `test_sign_macos` | yes | no | no | Signs a fixture bundle with `tools/release/sign_macos.sh`, ad hoc and with the identity `-`, and needs `cc`, `codesign` and `ditto`. It uses no certificate or keychain. |

The update requests do not need Qt's TLS. On Windows `createHttpTransport` returns a
WinHTTP transport, so `test_httptransport` and `test_selfupdater` exercise it there. They reach
their server on the loopback address over plain http only because the tests ask for it;
the transport of the application fetches https addresses only.

The telemetry sender posts through the same transport, so `test_telemetrynetwork_reject` and
`test_telemetrynetwork_trusted` run on Windows too, over https to a server on the loopback address.
WinHTTP checks that certificate against the system store and refuses it, which is what the reject
test expects. The trusted test gives the transport the test root certificate, and the transport checks
the certificate of the server against that root alone, so nothing is added to the certificate store.
Secure Transport, the TLS backend of Qt on macOS, does not trust that root certificate, so
`test_telemetrynetwork_trusted` reports itself as skipped there.

Every Bash smoke in the table except `test_package_contents_smoke` reads the
application path from `QEGTRAIN_APP`; `visual_polish_smoke.sh` passes it on to
the two scripts that its `scripts` phase runs. `test_creator_acceptance_smoke`
and `test_dialog_presentation_contract` also read the `scene_tool` path from
`QEGTRAIN_SCENE_TOOL`. CTest sets these variables from the build targets. Run by
hand without them, the scripts use the macOS paths under `build/`.
`test_route_diagram_smoke` reads no variable: CTest passes the application and
the directory of the test executables as the two arguments of
`route_diagram_smoke.py`, which uses the paths under `build/` when it gets none.

`test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`,
`test_route_diagram_smoke`, the five `test_visual_polish_smoke_*` tests and the end-to-end smokes
(`test_headless_smoke_case_1` to `test_headless_smoke_case_7`, `test_roundtrip_smoke`, `test_bundle_smoke`,
`test_incident_smoke` and `test_assignment_smoke`) are not registered in a
build with `EGTRAIN_ENABLE_SANITIZERS=ON`: the time limits of their scripts are set
for an uninstrumented build.

A build with sanitizers runs several times slower than the time limits in `CMakeLists.txt` allow.
`-DEGTRAIN_TEST_TIMEOUT_SCALE=N` multiplies the limit of every test that the top-level
`CMakeLists.txt` registers by N; the default 1 leaves the limits as they are. The characterization
tests that run committed scenes have a limit of their own, `-DEGTRAIN_CHARACTERIZATION_SCENE_TIMEOUT=SECONDS`
(default 300), which the scale does not change. Waits inside the tests follow the same settings:
`startup_launch_contract.py` multiplies its 60 second launch wait by the scale, and the fresh process
of one step in the `--repeat-files` tests may take five sixths of the scene limit. The `Sanitizers` job of `release.yml` passes a scale
of 10 and a scene limit of 5400 seconds, and runs `ctest --parallel 3` like the macOS leg of the CMake
workflow. The leak check passes only a scene limit of 1500 seconds.

`test_dialog_presentation_contract` runs `creator_acceptance_smoke.sh --dialog-contract`,
which starts the creator acceptance run and then checks the case chooser and the Run
simulation dialog in the built application, as [Dialog presentation](../architecture/dialog-presentation.md#checks)
describes.

`test_headless_scene_smoke`, `test_pe_image_size` and `test_pe_imports` run on every platform.
`test_headless_scene_smoke` starts the built QEGTRAIN headless on Paimpol and
checks the exit code, the `End of Simulation` line and the energy output. It also
starts a run with `--detailed-trajectories` and checks that `TEMP/Traj_Train_*.txt`
and `TrainTrajectories/TrainPathDiagram.txt` appear only in that run.
The tests `test_scene_version_run_*` also run on every platform. Each starts the
built QEGTRAIN headless on one runnable scene of `tests/fixtures/scene-versions`
and makes the same checks in both runs.

### HTTPS update check

`test_updatetransport_https` (labels `integration` and `network`) makes one real GET of
`https://api.github.com/repos/Ancientkingg/EGTRAIN/releases?per_page=1` through
`createHttpTransport` and reads the answer with `parseLatestStableRelease`. On Windows
that is the WinHTTP transport, so the test shows that a machine without a Qt TLS library
can make the request of the update check. It is registered only when the configuration
asks for it:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_NETWORK_TESTS=ON
ctest --test-dir build -L network --output-on-failure
```

On Windows add `-C Release` to the `ctest` command. Its output starts with `PASS`, `SKIP`
or `FAIL`. An answer with an HTTP status has reached the server, so the TLS handshake and
the certificate check worked, and the test tells the cases apart by what came back:

| Result | Cause |
| --- | --- |
| `PASS`, exit code 0 | The release list arrived and was read. A list whose newest release is a prerelease also passes. |
| `SKIP`, exit code 77 | GitHub answered with HTTP 403 or 429 (its rate limit) or with 5xx (an outage). CTest shows the test as skipped. |
| `FAIL`, exit code 1 | No HTTP response arrived: the name lookup, the connection, the TLS handshake or the certificate check failed, or 45 s passed. Also any other HTTP status, whose text says that it is not a TLS failure, and a body that is not a release list. |

The anonymous GitHub API allows 60 requests an hour for one address, and a run makes one
request. The program reads no rate-limit header, so every 403 counts as a rate limit.

`test_updatetransport_https_cases` runs the same program against a server on `127.0.0.1`
that gives each kind of answer, and runs in every configuration without the internet.

The Windows leg of the CMake workflow configures with `-DEGTRAIN_NETWORK_TESTS=ON`, leaves the
network tests out of the Test step with `-LE network`, and runs them in the step
Check HTTPS (Windows). The Windows package job builds the program with the package and runs
it in the step Check HTTPS from the package. That step copies the program into the copy of the
package that the start check used and runs it with only the Windows directories on `PATH`, so
it loads the Qt files of the package, as a machine without a developer setup would.

### Windows GUI tests

The offscreen Qt platform has no fonts of its own on Windows. CTest sets
`QT_QPA_FONTDIR` to the system font directory for every test with the `gui`
label. Set it yourself when you start a GUI test or QEGTRAIN with
`QT_QPA_PLATFORM=offscreen` by hand.

The tests that answer real message boxes, such as `test_scene_drop` with the
unsaved-changes prompt, run on the offscreen platform on Windows as well.

### Windows headless runs

QEGTRAIN is a GUI-subsystem program on Windows (`WIN32_EXECUTABLE`). With
`-g 0` it opens no window and reports only through redirected output handles
and its exit code (0 on success, 1 for bad arguments or scene errors). Started
from a terminal it prints nothing, and `& QEGTRAIN.exe` in PowerShell does not
wait for it. Start it from a program that creates the process and reads the
pipes, such as `tools/e2e/headless_scene_smoke.py`, or from PowerShell with
`Start-Process`; the two output files must differ:

```powershell
$dir = Resolve-Path .\build\Release
$p = Start-Process -Wait -PassThru -NoNewWindow -WorkingDirectory $dir `
  -FilePath "$dir\QEGTRAIN.exe" `
  -ArgumentList '--scene','Scenes/Paimpol','-h','300','-g','0','-pax','0' `
  -RedirectStandardOutput "$PWD\out.txt" -RedirectStandardError "$PWD\err.txt"
$p.ExitCode
```

The working directory is the one that holds `QEGTRAIN.exe`, where the build
copies `Scenes/`. Set `QEGTRAIN_OUTPUT_DIR` to keep the output out of the
profile directory.

### Windows package start

After the tests, the Windows CI leg assembles the files of the release package
in a temporary directory: `QEGTRAIN.exe`, the vcpkg DLLs, the Qt DLLs and
plugins that `windeployqt` adds, and the scenes, without the Visual C++ runtime
DLLs (the runner has the runtime, and the package job checks the imports). It
then runs
`tools/release/package_start_smoke.py` on that directory. The script removes
the Qt, vcpkg and library path variables from the environment and leaves only
the Windows directories on `PATH`, so a DLL or plugin that is missing from the
package fails the launch. It starts the packaged program in this order:

1. Headless on Paimpol to the end of a 120 s run.
2. With a window in startup timing mode, where the program opens the scene,
   prepares a run, paints it and exits.

A release package has no test hooks, so the script makes two more launches only
with `--exports`, for a package assembled from a build with
`EGTRAIN_BUILD_TESTS=ON`:

3. The CSV export.
4. The PNG export.

The Windows CI leg passes `--exports`, because it assembles the package from its build with tests. The
package workflows build release packages and do not.

The two export launches run Paimpol with a window and a horizon of 8000 s,
with the autostart hook and one export hook of the application each:
`QEGTRAIN_E2E_EXPORT_DIR` for the CSV files and `QEGTRAIN_E2E_ROUTE_DIAGRAM`
for the PNG files. The hook writes into a new temporary directory and then
exits the application, so each export is a launch of its own. The script
requires the marker that the application prints (`E2E_CSV_EXPORT_OK`,
`E2E_ROUTE_DIAGRAM_OK`) and the files that the hook names: the CSV files
`trajectory.csv`, `timetable.csv` and `run_summary.csv`, each not empty, with a
header line and a data line, and the PNG files `timetable_graph.png`,
`train_path_graph.png` and `route_reference_chooser.png`, each with a PNG
signature. It does not check the values in the files or how the images look.

Before the launches the script fails when a file of the package is an OpenMP
runtime library or names one (`vcomp140.dll` on Windows): the package ships
none.

The assembly uses the same `windeployqt` options as the Windows package job
(`--no-opengl-sw`, `--no-system-d3d-compiler`, `--no-system-dxc-compiler` and
`--no-quick-import` besides `--release`, `--no-translations` and
`--no-compiler-runtime`), and `test_ci_workflow` keeps the two command lines
equal. The Windows package keeps the image format plugins. It holds no `vc_redist.x64.exe`: see
[Windows package runtime](#windows-package-runtime).

### Windows package runtime

The installer installs for the current user and cannot run the Visual C++
redistributable, so the Windows package carries the runtime DLLs that its
program files import (for example `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` and
`MSVCP140.dll`) next to `QEGTRAIN.exe` (app-local deployment). Their
redistribution follows Microsoft's Visual C++ redistributable terms; the package
holds no notice file of its own for them. The
Universal CRT (`api-ms-win-crt-*`, `ucrtbase.dll`) is part of Windows 10 and 11
and is not copied. The step **Copy the Visual C++ runtime** of the Windows
package job finds the x64 folder `Microsoft.VC14*.CRT` below `VCToolsRedistDir`
and copies the DLLs that `tools/release/pe_imports.py --missing` lists for the
package. The step **Check the DLL imports of the package** runs
`tools/release/pe_imports.py dist/QEGTRAIN`, and the Finalize Windows job runs it
on the extracted portable archive. The script reads the import tables and the
delay-load import tables of every `.exe` and `.dll` below the directory and
exits 1 when one names a DLL that the package lacks and that is not in its list
of Windows system DLLs (`SYSTEM_DLLS` and the `api-ms-win-` and `ext-ms-win-`
prefixes). A new system DLL that the program starts to import goes into that
list. GitHub's Windows runners have the runtime installed system-wide, so
starting the package cannot show a missing runtime DLL; the import check does.
`test_pe_imports` tests the reader on PE files that it builds. Run the check by
hand on an extracted package with
`python3 tools/release/pe_imports.py <folder>`.

### macOS package start

The macOS package job removes the virtual keyboard input plugin with the Quick
and QML frameworks that it links, and the WebP and TIFF image plugins with
their libraries, from the app after `macdeployqt` and before the signature.
In the same place `tools/release/macos_build_paths.py fix` deletes the run
paths under `/opt/homebrew` and `/usr/local` and gives a file whose install
name is under one of them the name `@loader_path/NAME`; `macos_build_paths.py
check` then fails on any such path in the `otool -L`, `-D` or `-l` output of a
Mach-O file of the app (`test_macos_build_paths`). The CMake workflow assembles
no macOS package. The package job runs
`tools/release/package_start_smoke.py` on a copy of the app (step **Start the
package**).

For a package with an app bundle, the script first reads the load commands of
every Mach-O file with `otool -l`. It fails unless every dependency is a system
library (under `/System/Library/` or `/usr/lib/`) or a file inside the app, and
every library inside the app is loaded by some file. The search starts at the
files in `Contents/MacOS` and `Contents/PlugIns`, which no load command names:
the system starts a program, and Qt finds a plugin by its directory. The script
then starts the app as for Windows: the headless run and the window start. Only
the launch with a window loads the cocoa platform plugin of the bundle.

The macOS package keeps `QtDBus`, which `QtGui` loads, and the image format
plugins other than WebP and TIFF.

### Linux package start

The Linux package job extracts the AppImage that it built into a directory under
the runner temporary directory, so the file that is uploaded stays as built. The
job installs `xvfb` and `xauth` with the build tools. In step **Start the
package** it lists the plugin files of the extracted image in its log and runs
`tools/release/package_start_smoke.py` on the directory with `xvfb-run`, which
gives the program a virtual X display. The script starts the directory through
its `AppRun`, as the AppImage runtime does after it has mounted the image, and
makes the same launches as for Windows: the headless run and the window start.

Before the launches the script runs `ldd` on every ELF file below `usr/bin` and
`usr/plugins`. It fails unless each Qt and ZeroMQ library that such a file
loads is a file of the extracted image, and it fails when `ldd` lists no such
library at all. The runner has the Qt that the build used, so an image that
lacks a Qt library could otherwise still start there with the library of the
runner. The plugins are read as well as the programs, because
the platform plugin loads Qt libraries that the program does not. The job sets
no platform plugin variable, and the script removes every Qt variable and
`LD_LIBRARY_PATH` from the environment of the launches, so the image starts with
the platform plugin that it holds.

### Windows image size

Windows maps an EXE as one image whose size is `SizeOfImage` in the PE header,
and does not start an image near 2 GiB (it reports that the file is not a valid
Win32 application). `test_windows_image_size` fails when `SizeOfImage` of
`QEGTRAIN.exe` is above `EGTRAIN_MAX_PE_IMAGE_BYTES` (default 100000000), or
when the executable is not x64 and a Windows-subsystem program. A Release build
has an image of about 6 MB and a Debug build of about 18 MB, so the limit is
reached only when fixed-size static storage comes back. Set the variable at
configure time to change the limit. To check a build by hand:

```bash
python tools/release/pe_image_size.py build/Release/QEGTRAIN.exe --max-bytes 100000000
```

The first output line shows the measured size. The CI workflow prints the same
line in the job summary.

The same header holds the stack reserve. `QEGTRAIN.exe` is linked with 8 MiB,
the size the main thread has on macOS and Linux, and `test_windows_image_size`
checks that value. The other executables keep the linker default of 1 MiB, which
`test_windows_default_stack` checks on `scene_tool.exe`. On Windows the reserve
of an executable is also the stack size of every thread that is started without
one, so it is set for the application only. Measured on macOS: each of the six
committed scenes runs headless with the stack of the main thread limited to
128 KB, and none starts with 64 KB. To repeat it, run `ulimit -s 128` in a shell
and then the application with `--scene <scene folder> -g 0`.

## Verification Gates

To run what CI runs, use `ctest --test-dir build --output-on-failure` (add
`-C Release` with a multi-config generator, and `-j N` to run tests side by
side). For a quick check, use
`ctest --test-dir build -L unit -LE slow --output-on-failure`.

For UI changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/visual_polish_smoke.sh
```

For simulation, scene model, data conversion, or memory changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite runs `tools/e2e/headless_smoke.py` and `tools/e2e/roundtrip_smoke.py`.

For scene-format changes, also pack/unpack and validate/export the committed
scene directories with `build/scene_tool`. For UI or rendering changes, also run
`tools/e2e/visual_polish_smoke.sh`.

For documentation changes, run `python3 tools/docs/check_docs.py`; CTest runs it
as `docs_check` in a Git checkout, and its own tests as `test_check_docs`. The CMake
workflow runs it on every pull request. It checks
relative links, heading anchors, image references and alt text, images below
`docs/` that no document uses, documents below `docs/` that `docs/README.md`
does not reach through links and the table of contents in `docs/index.md`, links from
a page of the manual, `docs/guides/`, to a file outside `docs/guides/` and `docs/images/`, bold names in the manual
that are not strings of the application source, the syntax of `bash`, `sh` and `json` examples
(a `bash` or `sh` example with a here-document is skipped), em dashes and en
dashes, absolute local paths, Markdown that only GitHub renders (alerts,
`<details>`, links that start with `/`, and GitHub links to files in the
repository), and the heading order (one `#` heading first, no skipped levels). It prints one line per finding as
`path:line: [kind] message` and exits with 1 when it finds any. It exits with 2
when it cannot run, for example outside a Git repository. A `json` example must
be valid JSON, so an excerpt with comments or ellipses takes another info word
such as `text`. The script does not fetch external links and does not render
Markdown.

### Documentation site

The site is built from the pages of `docs/` with Sphinx, and the C++ reference from the
sources in `EGTRAIN/QEGTRAIN` with Doxygen. Its [table of contents](../index.md) is
`docs/index.md`, and its settings are `docs/conf.py` and `docs/Doxyfile`. Install Doxygen 1.18
or newer and Graphviz (on macOS `brew install doxygen graphviz`), then the Python packages in a
virtual environment in the build folder with Python 3.12 or newer, and configure again:

```bash
python3 -m venv build/docs-venv
build/docs-venv/bin/pip install -r docs/requirements.txt
cmake -S . -B build
```

On Windows the programs of the environment are in `build\docs-venv\Scripts`. Then:

```bash
cmake --build build --target docs
cmake --build build --target docs-preview
```

`docs` builds the reference into `build/docs/api/` and the site into `build/docs/`; open
`build/docs/index.html`. A Sphinx warning fails it. `docs-preview` serves the pages at
`http://127.0.0.1:8000` and rebuilds a page when its Markdown changes. It does not rebuild the
reference, so run `docs` first.

A page links to a class with the address of the published reference, such as
`https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_model.html`, so the link also
works on GitHub. The site build points such a link to the reference next to the page.
`build/docs-venv/bin/python tools/docs/check_undocumented.py main` lists the undocumented classes
and members that your branch adds since it forked from `main`, as the pull request check does.

The same environment builds the manual of the application. When `build/docs-venv` holds Sphinx at
configure time, the build runs Sphinx's `qthelp` builder with `-t manual` on `docs/index.md` and
`docs/guides/`, then `qhelpgenerator`, and copies `EGTRAIN.qch` next to the program, or into
`Contents/Resources` of the macOS app. The configure output says whether it does.
**Help > EGTRAIN Manual** and F1 open the section of the window that has the focus: a window or
dock names its section in the property `manualSection`, and `widgets/ManualWindow.cpp` maps each
section to a page. Without `EGTRAIN.qch` the application opens that page of the published guides
in the web browser. The package jobs build the manual and fail when a package lacks it.
