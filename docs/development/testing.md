# Testing

This page covers the CTest suite: how to run it, how to write a test, its labels, where the tests of each area are,
the tests each platform registers and the package checks. It ends with the checks to run for each kind of change and
the documentation site.

## Unit Tests

```bash
ctest --test-dir build --output-on-failure
```

With a multi-config generator such as Visual Studio, build and test the same configuration, for example
`cmake --build build --config Release` and `ctest --test-dir build -C Release`. `ctest -N` lists the tests registered
on the current platform.

Tests can run side by side: `ctest --test-dir build -j N --output-on-failure`. A test that starts the application
keeps its user settings in a folder of its own, most of them below `build/settings`, so none of them reads or changes
the settings of the user. CI runs `ctest --parallel` with the number of cores of the runner.

### Writing a test

The C++ tests use Qt Test. A test file is one test class (`tests/test_speedformat.cpp`):

```cpp
class TestSpeedFormat : public QObject {
	Q_OBJECT

private slots:
	void roundsToWholeKilometresPerHour(); // QCOMPARE(formatSpeedLabel(82.6), "83 km/h");
};

QTEST_APPLESS_MAIN(TestSpeedFormat)
#include "test_speedformat.moc"
```

- `egtrain_add_test` links Qt Test into every test that it builds. The file ends with `QTEST_APPLESS_MAIN` when it
  needs no Qt application, `QTEST_GUILESS_MAIN` for a `QCoreApplication` and `QTEST_MAIN` for widgets, which also sets
  `Qt::AA_Use96Dpi`.
- One slot per behaviour. A failed check ends its slot, so checks that must run after a failure go in a slot of their
  own. Setup goes into `initTestCase()` (once) or `init()` (before each slot), shared state into members, and every
  slot passes when it runs alone. Checks that build on what an earlier group left stay in its slot.
- `QCOMPARE(actual, expected)` shows both values on failure; use it where both sides print, such as numbers, `QString`
  and `std::string` (Qt 6.4 prints no value for it). A check that joined comparisons with `&&` becomes one line per
  comparison, in the same order. Anything else, a `nlohmann::json` comparison for example, is
  `QVERIFY2(condition, message)`.
- `QCOMPARE` treats floating-point values as equal within a relative 1e-12, and anything within 1e-12 of zero as zero.
  `QCOMPARE_EQ` compares exactly. Keep it to numbers: with a string literal, Qt 6.8.3 crashes when the comparison
  fails.
- A check over a table becomes a `_data()` slot with `QTest::addColumn` and `QTest::newRow`; each row reports on its
  own (`tests/test_timeformat.cpp`).
- clang-tidy does not know that a failed `QVERIFY` ends the slot. Before a pointer that may be null is used, check it
  with `if (p == nullptr) QFAIL("...");` (`tests/test_scenevalidator.cpp`).
- `QTRY_VERIFY_WITH_TIMEOUT(condition, milliseconds)` waits for a condition instead of a sleep. An exception that
  leaves a slot fails it and ends the program.
- `build/test_timeformat formatsSimTime` runs one slot, `formatsSimTime:offset` one row of it, and `-functions` lists
  the slots. A `gui` test outside CTest needs `QT_QPA_PLATFORM=offscreen`.
- Qt Test rejects arguments that it does not know. A test reads its paths from compile definitions of its
  registration, such as `EGTRAIN_TEST_SOURCE_DIR`, and a mode such as `EGTRAIN_UPDATE_EXPECTATIONS=1` from the
  environment (`tests/test_liveframe.cpp`). A test that starts itself as a child keeps a `main` of its own that
  handles its mode arguments before it calls `QTest::qExec` (`tests/test_recoverystore.cpp`).
- A Qt Test program that skips (`QSKIP`) exits with 0, so a test with `SKIP_RETURN_CODE` keeps a `main` of its own
  that returns the skip code before it calls `QTest::qExec`. So does a test that must set the environment before the
  application object exists (`tests/test_consolestream.cpp`).
- `tests/support/FakePrompts.h` answers the prompts of an editor panel without a dialog and keeps what the panel asked
  (`tests/test_editor_infrastructure_blocks.cpp`).
- `test_updatetransport_https` and the three programs in `tests/characterization/` are plain programs with a `main`,
  built with `add_executable`. The first returns 0, 77 or 1 ([HTTPS update check](#https-update-check)); the others
  compare their output with golden files ([Simulation characterization tests](characterization-tests.md)).

### Test labels

Every test has exactly one of `unit` or `integration`. Configuration fails when a test has neither or both, or a label
that this page does not describe.

- `unit`: runs in-process or is a static check. It starts no QEGTRAIN, `scene_tool`, update helper, or socket server.
- `integration`: starts or inspects a built program (`QEGTRAIN`, `scene_tool`, `egtrain_update_helper`, or a test that
  starts its own executable as a child) or talks to a loopback server.
- `gui`: needs a Qt platform plugin or launches QEGTRAIN in GUI mode. The label sets `QT_QPA_PLATFORM=offscreen`,
  apart from `test_widgetstyle` on Windows, which needs the windows platform. On Windows it also sets `QT_QPA_FONTDIR`
  to the system font directory, because the offscreen platform has no fonts of its own there; set it yourself when you
  start a GUI test or QEGTRAIN with `QT_QPA_PLATFORM=offscreen` by hand. Combine it with either of the labels above.
- `slow`: takes more than 20 seconds on a development machine.
- `network`: sends a request to a server on the internet. It is registered only with `-DEGTRAIN_NETWORK_TESTS=ON`, so
  no other test run needs the internet. Combine it with `integration`. See [HTTPS update check](#https-update-check).
- `scene-v1` covers the canonical scene model, `scene-v2` the `.egscene` container round-trips, hostile-archive checks
  and the released scene fixtures, and `legacy-compat` only the explicit importer/exporter boundary; normal simulation
  is not in that label.

To add a test and register its labels, see [Coding guidelines](coding-guidelines.md#tests). On Windows add
`-C Release` to each of these commands:

```bash
ctest --test-dir build -L unit -LE slow --output-on-failure
ctest --test-dir build -L unit -LE gui --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -L gui --output-on-failure
ctest --test-dir build -L scene-v2 --output-on-failure
ctest --test-dir build -LE legacy-compat --output-on-failure
```

### Tests by area

Most C++ tests `test_<name>` are built from `EGTRAIN/QEGTRAIN/tests/test_<name>.cpp`, and the comment at the top of a
test file or script says what it checks. [Smoke tests](smoke-tests.md) describes the end-to-end scripts, and
[Simulation characterization tests](characterization-tests.md) the golden-file tests of the simulation core.

| Area | Tests | What they cover |
| --- | --- | --- |
| Scene format and files | `test_scenevalidator`, `test_scenereferences`, `test_sceneimporter`, `test_sceneexporter`, `test_scenewriter`, `test_scenebundle`, `test_scenecompatibility`, `test_sceneversions`, `test_sceneopenrequest`, `test_scenesavepolicy`, `test_routestations`, `tools/e2e/test_scene_codec_contract.py`, `tools/e2e/test_scene_tool_diagnostics.py` | Loading, validating, writing, importing and exporting a scene, `.egscene` bundles, the scenes of older versions and the messages of `scene_tool`. |
| Scene editing and the editor panels | `test_editor_*`, `test_infrastructureguidance`, `test_scenedocument`, `test_scenehistory`, `test_stopinsertion`, `test_trainunitsourcelinker`, `test_editorlayout` | Each panel of `editors/` over a `SceneDocument`, Undo and Redo, and the unsaved state of a document. |
| Simulation | `tests/characterization/`, `test_scenebuilder`, `test_operationsbuilder`, `test_stopdwell`, `test_simulationrun`, `test_simulationworker`, `test_trackpreview`, `test_trajectory`, `tools/e2e/test_signalling_bounds.py` | The builders from the scene model to a run, the run state, the worker thread and the golden files of the simulation core. |
| Results and charts | `test_runresults`, `test_completedrun`, `test_delayanalysis*`, `test_capacity*`, `test_areacapacity*`, `test_blockingtimediagram`, `test_routediagramcoordinates`, `test_positionscale`, `test_routecharts`, `test_routegeo`, `test_routereferencechoice`, `test_charticks`, `test_diagramwindow`, `test_timetabletablewindow`, `test_traincolours`, `test_tractioncurve`, `test_csvwriter`, `tools/e2e/test_diagrams_no_sim_globals.py` | The result windows of `diagrams/`, the numbers they show and their CSV and PNG exports. |
| The main window and run control | `test_network*`, `test_runtimecanvas`, `test_followcamera`, `test_followavailability`, `test_stationoverlay`, `test_visualpolish`, `test_highlighteffect`, `test_inspectorpanel`, `test_guisimulationsnapshot`, `test_replaycoverage`, `test_runoutcome`, `test_runselection`, `test_playbackprofiler`, `test_live*` | The network canvas and its presenter, playback and replay, the outcome of a run and the live stream. |
| Dialogs, widgets and messages | `test_casechooserdialog`, `test_settingsdialog`, `test_aboutdialog`, `test_manualwindow`, `test_dialoglayout`, `test_userprompts`, `test_widgetstyle`, `test_lightpalette`, the widget tests such as `test_choicecombo` and `test_elidedlabel`, `test_cannotopenmessage`, `test_pathmessages`, `test_dialogfolders`, `test_timeformat`, `test_speedformat`, `test_version`, `test_log`, `test_consolestream`, `test_guardedapplication`, `test_qt_resource_images` | The dialogs and widgets of `widgets/`, the texts of messages and the small helpers of `util/`. |
| Recovery, crashes and opening files | `test_recovery*`, `test_crashnote`, `test_openrequestrelay`, `test_startup_launch_contract`, `test_case_chooser_contract` | Recovery copies, the crash note, and a file that a second launch or the system hands to the running window. |
| Updates and telemetry | `test_update*`, `test_httptransport`, `test_selfupdater`, `test_releasenotes`, `test_distribution`, `test_telemetry*` | The update check and its transport, staging and installing an update, and the telemetry consent, queue and sender. `tools/e2e/test_telemetry_contract.py` runs in the telemetry contract workflow, not in CTest. |
| Packages and release scripts | `tools/release/test_*`, `tools/ci/test_changed_files.py`, `tools/e2e/test_ci_workflow.py`, `test_windows_image_size`, `test_windows_default_stack`, `test_win32_configure_rejected` | The release, packaging and signing scripts, the PE header of the Windows programs and the workflows. `test_notarize_macos.sh` runs in the macOS package job, not in CTest. |
| Smoke tests | `tools/e2e/*_smoke.*`, `tools/e2e/*_contract.py`, `tools/e2e/test_case3_startup_errors.py`, `tools/e2e/test_headless_smoke_decode.py`, `test_headless_scene_smoke`, `test_scene_version_run_*`, and the scripted window runs of `tests/e2e/` | The built application end to end on the committed scenes ([Smoke tests](smoke-tests.md)). |
| Source and tool checks | `tools/e2e/test_logging_contract.py`, `test_no_e2e_in_product.py`, `test_qt_resources.py`, `test_app_icon_config.py`, `test_windows_subsystem_config.py`, `tools/memory/test_*.py`, `tools/performance/test_playback_profile.py` | Static checks of the application source and its resources, and the tests of the memory and profiling tools. |
| Documentation tools | `tools/docs/test_check_docs.py`, `tools/docs/test_check_undocumented.py`, `docs_check`, `test_capture_screenshots` | The documentation checks and the capture of the pictures ([Documentation pictures](smoke-tests.md#documentation-pictures)). |

A case that needs POSIX permissions or file size limits, or the behaviour of one system, reports itself as skipped
elsewhere, and the permission cases also for a process that may write into a read-only folder, as one with root rights
may. `test_crashnote` and the crash note case of `test_recovery_smoke` crash programs on purpose, so a system crash
report of each is expected. `test_openrequestrelay` and `test_startup_launch_contract` put their socket files in a
folder directly below `/tmp` on macOS and Linux, because a socket path is limited to about 100 characters. The two
`test_telemetrynetwork_*` tests start `tools/e2e/telemetry_tls_receiver.py`, an https server on the loopback address.

### Scene-version fixtures

`test_sceneversions` reads the scenes in `tests/fixtures/scene-versions`. They are byte copies of scenes written by
older builds, listed with their origin in `manifest.json`, and a fixture is never edited or saved again. The test
fails when a version number is raised without a fixture of the previous number, as the
[V1 Scene Model](../architecture/scene-model.md#compatibility-boundary) describes.

A release that raises `kCurrentSceneSchemaVersion` or `kCurrentSceneBundleVersion` changes the scene format. After it
is published, download one of its bundles from the release and copy it unchanged into `tests/fixtures/scene-versions`.
Add an entry to `manifest.json` with the release in `origin` and the other keys of an existing entry (`counts` come
from the loaded model). If the bundle is meant to run, add a `test_scene_version_run_*` test next to the existing ones
in `EGTRAIN/QEGTRAIN/tests/CMakeLists.txt`, with the same `TIMEOUT` and the label `integration`. The next raise of a
number then finds a fixture of the current one.

### Platform coverage

CMake prints one `Windows: skipping ...` status line at configure time for each group of tests it leaves out. `yes`
below means the test is registered on that platform, and `partly` that it is registered and only some of its checks
run there.

| Test | macOS | Linux | Windows | Reason |
| --- | --- | --- | --- | --- |
| `test_csv_export_smoke`, `test_creator_acceptance_smoke`, `test_dialog_presentation_contract`, `test_editor_smoke` | yes | yes | no | Bash scripts; Windows has no bash on `PATH` that can be relied on. |
| `test_recovery_smoke` | yes | yes | no | The lock files of the recovery copies are not checked after a crash on Windows. |
| `test_crashnote` | yes | yes | partly | On Windows the signal and exception cases run on the main thread only, and the notes have no stack. |
| `test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`, `test_route_diagram_smoke` | yes | yes | no | The first three are Bash scripts, and `test_route_diagram_smoke` is registered with them under `if(UNIX)`. |
| `test_visual_polish_smoke_live`, `test_visual_polish_smoke_replay`, `test_visual_polish_smoke_replay_copenhagen`, `test_visual_polish_smoke_signals`, `test_visual_polish_smoke_stations` | yes | no | no | Registered on macOS only. The script also runs on Linux with `QEGTRAIN_APP=$PWD/build/QEGTRAIN`. |
| `test_capture_screenshots` | yes | no | no | Takes the pictures of `docs/images` with the macOS app bundle. |
| `test_live_stream_smoke` | yes | partly | partly | The second run in one process uses a scripted window run, which is checked on macOS only. |
| `test_case_chooser_contract` | yes | yes | no | Compares backslash paths with forward-slash paths, writes them into a `QSettings` INI file where backslash is an escape, and creates a directory symlink. |
| `test_package_contents_smoke` | yes | no | no | Checks the `.app` bundle with macOS tools. |
| `test_measure_peak_rss` | yes | yes | no | Tests the collector for the macOS `/usr/bin/time -l`, without that tool. |
| `test_windows_image_size`, `test_windows_default_stack` | no | no | yes | Read the PE header of `QEGTRAIN.exe` and `scene_tool.exe`. |
| `test_win32_configure_rejected` | no | no | yes | Configures for 32-bit Windows with the Visual Studio generator and expects the message that only x64 is supported. |
| `test_verify_signatures` | no | no | yes | Needs PowerShell and its Authenticode commands; not registered when no PowerShell is found. |
| `test_startup_launch_contract` | yes | yes | partly | The pseudo-terminal launches, the `--interactive` launches, the window launch with a bundle argument, the open events and the file launches while a window runs need macOS or Linux. |
| `test_updatepreparation` | partly | partly | partly | The disk image and zip checks run on macOS only and need `hdiutil`, `ditto` and `codesign`; the AppImage check runs on Linux only. |
| `test_make_dmg` | yes | partly | partly | The image, mount and signature checks need `hdiutil`, `ditto` and `codesign`. |
| `test_sign_macos`, `test_package_macos` | yes | no | no | Run the macOS release scripts on a fixture bundle and need `cc` and `codesign`, but no signing identity. |

Every Bash smoke in the table reads the application path from `QEGTRAIN_APP`, which `visual_polish_smoke.sh` passes on
to the two scripts of its `scripts` phase, except `test_package_contents_smoke`, which reads the app bundle from
`QEGTRAIN_APP_BUNDLE`. It, `test_creator_acceptance_smoke` and `test_dialog_presentation_contract` also read the
`scene_tool` path from `QEGTRAIN_SCENE_TOOL`. CTest sets these variables from the build targets; run by hand without
them, the scripts use the macOS paths under `build/`. CTest passes `route_diagram_smoke.py` the application and the
directory of the test executables as arguments, and without them it uses the paths under `build/`.

On Windows `createHttpTransport` returns a WinHTTP transport, so `test_httptransport`, `test_selfupdater` and the two
`test_telemetrynetwork_*` tests exercise it there. Secure Transport, the TLS backend of Qt on macOS, does not trust
the test root certificate, so `test_telemetrynetwork_trusted` reports itself as skipped on macOS.

A build with `EGTRAIN_ENABLE_SANITIZERS=ON` leaves out the smokes whose scripts have time limits for an uninstrumented
build: `test_headless_smoke_case_*`, `test_roundtrip_smoke`, `test_bundle_smoke`, `test_incident_smoke`,
`test_assignment_smoke`, `test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`,
`test_route_diagram_smoke`, the visual polish smokes and `test_capture_screenshots`. `-DEGTRAIN_TEST_TIMEOUT_SCALE=N` multiplies by N the limit of every test that
`egtrain_add_test` registers, the 300 s after which Qt Test stops a slot (`QTEST_FUNCTION_TIMEOUT`) and the waits of
scripts such as `startup_launch_contract.py`. The characterization tests that run committed scenes take
`-DEGTRAIN_CHARACTERIZATION_SCENE_TIMEOUT=SECONDS` (default 300) instead. The sanitizer jobs pass a scale of 10 and a
scene limit of 5400 s, and the leak check a scene limit of 1500 s
([Nightly sanitizer runs](smoke-tests.md#nightly-sanitizer-runs)).

### HTTPS update check

`test_updatetransport_https` (labels `integration` and `network`) makes one real GET of the GitHub release list
through `createHttpTransport`, which is WinHTTP on Windows, and reads the answer with `parseLatestStableRelease`. It
is registered only when the configuration asks for it (on Windows add `-C Release` to `ctest`):

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_NETWORK_TESTS=ON
ctest --test-dir build -L network --output-on-failure
```

Its output starts with `PASS` (exit code 0: the release list was read), `SKIP` (77: GitHub answered with HTTP 403 or
429, its rate limit, or with 5xx, an outage) or `FAIL` (1: no HTTP response within 45 s, another HTTP status, or a
body that is not a release list). CTest shows a skip as skipped. The anonymous GitHub API allows 60 requests an hour
for one address, and a run makes one. `test_updatetransport_https_cases` runs the same program against a server on
`127.0.0.1` and needs no internet. The Windows leg of the CMake workflow leaves the network tests out of its Test step
(`-LE network`) and runs them in the step Check HTTPS (Windows), and the Windows package job runs the program from a
copy of the package ([CI and release](ci-and-release.md)).

### Windows headless runs

QEGTRAIN is a GUI-subsystem program on Windows (`WIN32_EXECUTABLE`). With `-g 0` it opens no window and reports only
through redirected output handles and its exit code (0 on success, 1 for bad arguments or scene errors). Started from
a terminal it prints nothing, and `& QEGTRAIN.exe` in PowerShell does not wait for it. Start it from a program that
creates the process and reads the pipes, such as `tools/e2e/headless_scene_smoke.py`, or from PowerShell with
`Start-Process`; the two output files must differ:

```powershell
$dir = Resolve-Path .\build\Release
$p = Start-Process -Wait -PassThru -NoNewWindow -WorkingDirectory $dir `
  -FilePath "$dir\QEGTRAIN.exe" `
  -ArgumentList '--scene','Scenes/Paimpol','-h','300','-g','0','-pax','0' `
  -RedirectStandardOutput "$PWD\out.txt" -RedirectStandardError "$PWD\err.txt"
$p.ExitCode
```

The working directory is the one that holds `QEGTRAIN.exe`, where the build copies `Scenes/`. Set
`QEGTRAIN_OUTPUT_DIR` to keep the output out of the profile directory.

### Package checks

The package jobs check each package with the scripts in `tools/release/`, as [CI and release](ci-and-release.md)
describes. The docstring of each script gives its options. To repeat a check by hand:

- `python3 tools/release/package_start_smoke.py <package>` starts a package folder with only the system directories on
  the search path: headless on Paimpol, then with a window in startup timing mode. `--exports` adds a CSV and a PNG
  export, which need a build with `EGTRAIN_BUILD_TESTS=ON`. The Windows leg of the CMake workflow passes it for the
  package that it assembles from its build. On Linux, start the extracted AppImage with `xvfb-run`.
- `python3 tools/release/pe_imports.py <folder>` fails when a program file imports a DLL that is neither in the folder
  nor a Windows system DLL. A system DLL that the program starts to import goes into `SYSTEM_DLLS` of the script.
- `python tools/release/pe_image_size.py build/Release/QEGTRAIN.exe --max-bytes 100000000` prints `SizeOfImage` of the
  PE header; Windows does not start an image near 2 GiB. `test_windows_image_size` fails above
  `EGTRAIN_MAX_PE_IMAGE_BYTES` (set at configure time, default 100000000) and also checks the 8 MiB stack reserve of
  `QEGTRAIN.exe`, and `test_windows_default_stack` the linker default of 1 MiB of `scene_tool.exe`.
- `python3 tools/release/macos_build_paths.py check <app>` fails on a path under `/opt/homebrew` or `/usr/local` in a
  Mach-O file of the app, and `fix` removes such paths.

The Windows leg assembles its package with the `windeployqt` options of the Windows package job, and
`test_ci_workflow` keeps the two command lines equal.

## Verification Gates

To run what CI runs, use `ctest --test-dir build --output-on-failure` (add `-C Release` with a multi-config generator,
and `-j N` to run tests side by side). For a quick check, use
`ctest --test-dir build -L unit -LE slow --output-on-failure`. For a change of the code, build and run the suite, and
for a UI or rendering change also the visual polish smoke:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/visual_polish_smoke.sh
```

The suite runs `tools/e2e/headless_smoke.py` and `tools/e2e/roundtrip_smoke.py`, the checks for simulation, scene
model, data conversion and memory changes. For scene-format changes, also pack/unpack and validate/export the
committed scene directories with `build/scene_tool`.

For documentation changes, run `python3 tools/docs/check_docs.py`. It checks the links, anchors, images and code
examples of the Markdown files, the pages that `docs/README.md` does not reach and the style rules of the pages. In
the manual, every bold name must be a string of the application or a term that the script lists. It prints one line
per finding as `path:line: [kind] message` and exits with 1 when it finds any. The docstring at the top of
`tools/docs/check_docs.py` lists every rule; `--help` shows only the options. CTest runs the script as `docs_check` in
a Git checkout, and its own tests as `test_check_docs`. The CMake workflow runs it on every pull request.

### Documentation site

The site is built from the pages of `docs/` with Sphinx, and the C++ reference from the sources in `EGTRAIN/QEGTRAIN`
with Doxygen. Its [table of contents](../index.md) is `docs/index.md`, the start page of the reference is
`docs/mainpage.dox`, and the settings are `docs/conf.py` and `docs/Doxyfile`. Install Doxygen 1.18 or newer and
Graphviz (on macOS `brew install doxygen graphviz`), then the Python packages in a virtual environment in the build
folder with Python 3.12 or newer, and configure again:

```bash
python3 -m venv build/docs-venv
build/docs-venv/bin/pip install -r docs/requirements.txt
cmake -S . -B build
cmake --build build --target docs
cmake --build build --target docs-preview
```

On Windows the programs of the environment are in `build\docs-venv\Scripts`. `docs` builds the reference into
`build/docs/api/` and the site into `build/docs/`; open `build/docs/index.html`. A Sphinx warning fails it.
`docs-preview` serves the pages at `http://127.0.0.1:8000` and rebuilds a page when its Markdown changes. It does not
rebuild the reference, so run `docs` first.

A page links to a class with the address of the published reference, such as
`https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_model.html`, so the link also works on GitHub. The
site build points such a link to the reference next to the page.
`build/docs-venv/bin/python tools/docs/check_undocumented.py main` lists the undocumented classes and members that
your branch adds since it forked from `main`, as the pull request check does.

The same environment builds the manual of the application. When `build/docs-venv` holds Sphinx at configure time, the
build runs Sphinx's `qthelp` builder with `-t manual` on `docs/index.md` and `docs/guides/`, then `qhelpgenerator`,
and copies `EGTRAIN.qch` next to the program, or into `Contents/Resources` of the macOS app. The configure output says
whether it does. **Help > EGTRAIN Manual** and F1 open the section of the window that has the focus: a window or dock
names its section in the property `manualSection`, and `widgets/ManualWindow.cpp` maps each section to a page. Without
`EGTRAIN.qch` the application opens that page of the published guides in the web browser. The package jobs build the
manual and fail when a package lacks it.
