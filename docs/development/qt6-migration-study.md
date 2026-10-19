# Qt 6 migration study

This study, made on 2026-10-19 at `5ad2af0a`, looks at what moving EGTRAIN from Qt 5.15 to Qt 6 takes. Nothing was
ported. Qt 6 was not installed: the code findings come from a Qt 5.15 build with the deprecated APIs disabled and from
searches of the sources, and the Qt 6 facts come from the Qt documentation and the Homebrew formulae cited at each
point. Estimates are marked as estimates.

## Summary

- The code already builds against Qt 5.15 with every API that Qt 5.15 deprecates disabled: 0 errors. It uses none of
  `QRegExp`, `QTextCodec`, `QStringRef`, `QLinkedList` or `QDesktopWidget`.
- What remains for Qt 6 is small in the sources and larger in the build and release setup: the CMake targets, three
  CI platforms, three package jobs and the release scripts name Qt 5.
- Qt Charts, which draws the result diagrams, is deprecated since Qt 6.10. It still ships with Qt 6.12, so a port can
  keep it, but its replacement is a separate decision.
- Licensing does not change: the modules EGTRAIN ships stay compatible with GPL-3.0-only.
- Recommendation: make the code build with both Qt 5.15 and Qt 6, add a Qt 6 CI build next to the Qt 5 one, port the
  packages, then switch the releases. Homebrew stops installing `qt@5` on 2027-05-19, which sets the deadline for the
  macOS jobs.

## Source code

### Build with deprecated APIs disabled

The application and all tests were built in a separate build directory with Qt 5.15.19 from Homebrew `qt@5`, Ninja,
two jobs and `-k 0`, so that a failing file would not stop the build:

```bash
cmake -S . -B build-nodeprecated -G Ninja -DEGTRAIN_IPO=OFF -DEGTRAIN_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5 -DCMAKE_CXX_FLAGS=-DQT_DISABLE_DEPRECATED_BEFORE=0x050F00
cmake --build build-nodeprecated --parallel 2 -- -k 0
```

All 696 build steps succeeded with 0 errors. 420 of the 421 compile commands carried the define; the other one is the
C file of miniz. The 15 warnings in the log are not about Qt: duplicate libraries at link time, an implicit copy
constructor of `TrainEvent` and an ignored `nodiscard` result. In Qt 5.15 `QT_DEPRECATED_SINCE(5, 15)` is false when the define is `0x050F00`
(`QtCore/qglobal.h`, line 339), so the APIs deprecated in 5.15 itself are removed too. A test file that called
`qrand()` and `QString::SkipEmptyParts` compiled with warnings without the define and failed with it. Qt 6 names the
same macro `QT_DISABLE_DEPRECATED_UP_TO` ([porting guide](https://doc.qt.io/qt-6/portingguide.html)).

### Changes the Qt 5 build cannot show

Some Qt 6 changes are not deprecations in Qt 5.15. The counts below come from searches of `EGTRAIN/QEGTRAIN`
without `io/third_party`.

| Qt 6 change | Uses in EGTRAIN | Effect |
| --- | --- | --- |
| `QRegExp`, `QStringRef` moved to Qt5Compat, `QTextCodec` removed from Qt Core ([Qt Core changes](https://doc.qt.io/qt-6/qtcore-changes-qt6.html)) | 0 | none |
| Qt Charts has no `QtCharts` namespace ([Qt Charts changes](https://doc.qt.io/qt-6/qtcharts-changes-qt6.html)); `qchartglobal.h` of Qt 6.8 no longer defines `QT_CHARTS_USE_NAMESPACE` | 4: app 2 (`MainWindow.cpp:75`, `ResultViews.cpp:51`), diagrams 1 (`DiagramWindow.h:25`), tests 1 | compile error |
| `QNativeGestureEvent::localPos()` deprecated in 6.0; `QMouseEvent::pos()` is not (`qevent.h` of Qt 6.8, lines 220 and 403) | 2: diagrams 1 (`DiagramWindow.cpp:1220`), graphics 1 (`NetworkView.cpp:257`); Qt 5.15 has no `QNativeGestureEvent::position()` | warning |
| Five-argument `QMouseEvent` constructor deprecated in 6.4, old `QNativeGestureEvent` constructor in 6.2 (`qevent.h`, lines 201 and 376) | tests: 20 and 2 | warning |
| `QList` and `QString` sizes are `qsizetype`, not `int` ([Qt Core changes](https://doc.qt.io/qt-6/qtcore-changes-qt6.html)) | 27 declarations of the form `int x = list.size()` or `indexOf(...)`: tests 12, app 6, telemetry 5, graphics 2, widgets 1, update 1; one `std::min` of an `int` and a size (`telemetry/TelemetrySender.cpp:491`) | MSVC warning C4244; the `std::min` call fails to compile with every compiler |
| `QVariant::operator==` compares through `QMetaType::equals`, floating point exactly, and no longer converts between different non-numeric types; `operator<` removed | no ordering comparison; equality through `findData` or `data() ==`: editors 21, app 3, diagrams 1, tests 68 | review |
| A size-changing `QList` call invalidates references to its elements | not countable by search | review |

The warnings matter because CI configures with `-DEGTRAIN_WARNINGS_AS_ERRORS=ON`
([cmake.yml](../../.github/workflows/cmake.yml), lines 194 to 211) and the libraries other than the simulation, and
most tests, are strict targets ([CMakeLists.txt](../../CMakeLists.txt), lines 413 to 417), so a warning in them fails a
Qt 6 CI build. The `QEGTRAIN` target, which builds most of `app/`, and the tests that use the simulation get no warning
flags (lines 421 to 430). The `qsizetype` count is a lower bound: it misses sizes passed as `int` arguments or returned
from `int` functions, which MSVC at `/W4` reports too. Clang and GCC do not warn about these conversions with `-Wall
-Wextra`.

Outside the sources: [CMakeLists.txt](../../CMakeLists.txt) names Qt 5 on 97 lines (`find_package(Qt5 ...)` on line
115, `Qt5::` targets, `Qt5::WinMain` on line 626). The MSVC forced include `util/QtMsvcCompat.h` was written for
the Qt 5.15 headers. Eleven files under `tools/` name Qt 5, among them `tools/tidy.py:70`,
`tools/release/package_start_smoke.py:70` and `tools/e2e/test_qt_resources.py:62`.

## Packaging

| Platform | Today | With Qt 6 |
| --- | --- | --- |
| macOS | `macdeployqt` of Homebrew `qt@5`, then the trimming of #488 removes Qt Quick, QML, the virtual keyboard and the WebP and TIFF plugins ([package.yml](../../.github/workflows/package.yml), lines 109 to 127) | `macdeployqt` remains. Homebrew Qt 6 is split into formulae: `qtbase`, `qtcharts` and `qtsvg` are enough, and `qtcharts` depends on `qtdeclarative`. `qtbase` depends on `icu4c@78` and `openssl@4` and links OpenSSL (Homebrew `qtbase.rb`, lines 62, 67 and 141), so the bundle can gain ICU and OpenSSL libraries; HarfBuzz, double-conversion, libb2 and D-Bus are further `qtbase` dependencies on macOS, and step 4 decides which of them `macdeployqt` bundles. The trimming steps and the checks on lines 200 to 213 use framework names that Qt 6 keeps. |
| Windows | `windeployqt` with `--no-angle` and `--no-virtualkeyboard` ([cmake.yml](../../.github/workflows/cmake.yml), line 254; [package.yml](../../.github/workflows/package.yml), line 525); checks name `Qt5Core.dll`, `styles/qwindowsvistastyle.dll` and others (lines 569, 575 and 583) | The Qt 6 `windeployqt` has neither option ([Qt for Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html)); `--skip-plugin-types` and `--exclude-plugins` replace them. The DLLs become `Qt6*.dll`, and the style plugin of Qt 6.8.3 `win64_msvc2022_64` is `styles/qmodernwindowsstyle.dll`. TLS is a plugin, `qschannelbackend` ([Qt SSL](https://doc.qt.io/qt-6/ssl.html)). |
| Linux | `linuxdeploy-plugin-qt` with the Ubuntu Qt 5 packages; the `QMAKE` fallback names `qmake-qt5` (line 1010); the checks look for `libQt5*.so` (lines 1033 to 1035) | The plugin supports Qt 5 and 6 ([README](https://github.com/linuxdeploy/linuxdeploy-plugin-qt)). The `QMAKE` line, the checks and `package_start_smoke.py` need the Qt 6 names. |

Platform support compared with what the README promises:

| | README | Qt 6.8 ([platforms](https://doc.qt.io/qt-6.8/supported-platforms.html)) | Qt 6.12 ([platforms](https://doc.qt.io/qt-6/supported-platforms.html)) |
| --- | --- | --- | --- |
| macOS | macOS 26 or later, Apple silicon | macOS 12 or later | macOS 14.4 or later |
| Windows | 64-bit Windows; the build guide says Windows 10 or 11 | Windows 10 1809 or later, MSVC 2022 | Windows 10 1809 or later, MSVC 2022; "the last version to support Windows 10" |
| Linux | 64-bit AppImage, built on `ubuntu-latest` | binaries need glibc 2.28 | binaries need glibc 2.34 |

Every promise holds with either version. Windows 10 support ends after Qt 6.12, which matters for later upgrades only.

## CI

| Platform | Today | Qt 6 option |
| --- | --- | --- |
| macOS | `brew install qt@5` in `cmake.yml`, `package.yml` and `release.yml` | `brew install qtbase qtcharts qtsvg` (6.12.0), or `install-qt-action` with host `mac` |
| Windows | `jurplel/install-qt-action@v4`, Qt 5.15.2 `win64_msvc2019_64` | the same action, `win64_msvc2022_64`; its default is 6.8.3, "a tested Qt 6 LTS version for open-source licensed users" ([install-qt-action](https://github.com/jurplel/install-qt-action)) |
| Linux | Ubuntu 24.04 Qt 5 packages, also in `tidy.yml` | Ubuntu 24.04 has Qt 6.4.2 (`qt6-charts-dev 6.4.2-5build2`, [packages.ubuntu.com](https://packages.ubuntu.com/noble/qt6-charts-dev)); `install-qt-action` gives the version that the other platforms use |

After 2027-05-19 `brew install qt@5` fails: Homebrew blocks a disabled formula and removes it from `homebrew/core`
one year after the disable date ([Homebrew](https://docs.brew.sh/Deprecating-Disabling-and-Removing)). The macOS CI,
package and release jobs stop working that day unless they install Qt 5.15 another way or use Qt 6.

Open-source users get only the first patch releases of an LTS version; later ones are for commercial customers. Qt 6.8
has 6.8.8 as a commercial release, Qt 6.12 is listed with standard support until 2031-09-30
([Qt releases](https://doc.qt.io/qt-6/qt-releases.html)). For open-source builds this means following the current
minor release.

Build time. In CI run 39217890924 on `main` the Qt installation took 20 s (macOS), 37 s (Windows, cached) and 36 s
(Linux, apt), and the build step took 25 s, 72 s and 17 s with the compiler cache. A Qt change invalidates that cache
once. Estimate: the first Qt 6 build costs as long as an uncached build today; later builds are similar. The extra
`qtdeclarative` download on macOS and the Qt 6 header cost were not measured.

Package size. The v1.0.2 assets are 35.5 MB (AppImage), 20.4 MB (macOS zip, before the trimming of #488) and 44.0 MB
(Windows zip). Estimate: with Homebrew Qt 6 the macOS app gains the ICU libraries if Qt Core links them; their data
library alone is 33,195,120 bytes uncompressed on the machine of this study. The official Qt binaries for Windows and
Linux were not measured.

## High-DPI and look

- Qt 6 always scales for high-DPI screens ([porting guide](https://doc.qt.io/qt-6/portingguide.html)), and the default
  rounding policy is `PassThrough`, so 125 % or 150 % in Windows gives a device pixel ratio of 1.25 or 1.5
  ([High DPI](https://doc.qt.io/qt-6/highdpi.html)). EGTRAIN sets no Qt 5 high-DPI attribute (`app/main.cpp:435`), so
  today it does not scale on Windows and X11. On macOS both versions use whole ratios of 1 or 2.
- Network view: the drawing already uses the device pixel ratio (`graphics/items/SignalItem.cpp:108`,
  `graphics/items/StationOverlayItem.cpp:419`) for Retina screens. Fractional ratios were tried only on Linux with
  `QT_SCALE_FACTOR=1.5` ([feature status](../product/feature-status.md), runs of 2026-10-17, which found #883 and
  #889), not with Windows display scaling: hairlines, cached pixmaps and station pictograms need a look at 125 % and
  150 %.
- Diagrams: Qt Charts draws in a `QGraphicsView` like the network view, with the same check. The pixel tests run on
  the offscreen platform and read the ratio of each image, so they should not be affected.
- Stylesheet: `resources/styles/egtrain.qss` (452 lines) gives sizes in `px`, which Qt 6 scales; 1 px borders at 1.5
  can come out uneven. It sets the background and text colour of every `QWidget` (lines 1 to 4), which keeps the light
  look. Since Qt 6.5 Qt loads the system palette ([Qt blog](https://www.qt.io/blog/dark-mode-on-windows-11-with-qt-6.5)),
  and since Qt 6.7 the Windows 11 style is the default on Windows 11
  ([what's new in 6.7](https://doc.qt.io/qt-6/whatsnew67.html)), so parts the stylesheet does not cover can look
  different in a dark Windows.

## Licences

| Module that would ship | Licence | With GPL-3.0-only |
| --- | --- | --- |
| Qt Core, Gui, Widgets, Network, Svg, DBus (macOS, Linux), PrintSupport (macOS) | LGPL-3.0, GPL-2.0 or GPL-3.0 (Homebrew `qtbase` and `qtsvg`) | compatible |
| Qt Charts | GPL-3.0 ([Qt Charts](https://doc.qt.io/qt-6/qtcharts-index.html)) | compatible, as today |
| Qt Graphs, if it replaces Qt Charts | GPL-3.0 ([Qt Graphs](https://doc.qt.io/qt-6/qtgraphs-index.html)) | compatible |
| ICU, OpenSSL and the other `qtbase` dependencies (HarfBuzz, double-conversion, libb2, D-Bus), with Homebrew `qtbase` on macOS | Unicode licence; Apache-2.0 for OpenSSL 3 and later; MIT, BSD, CC0 or Apache-2.0, and GPL-2.0-or-later for the others | compatible; add to `THIRD_PARTY_NOTICES.md` |

The third-party code inside Qt 6 is listed at [Licenses used in Qt](https://doc.qt.io/qt-6/licenses-used-in-qt.html).
`THIRD_PARTY_NOTICES.md` names Qt 5.15 on lines 7 to 9 and needs the Qt 6 version.

## Estimates

| Area | Work | Estimate |
| --- | --- | --- |
| CMake | versionless targets, find Qt 6 or 5.15, `Qt::WinMain` | 0.5 day |
| Sources | 4 namespace lines, 2 gesture calls, 22 test constructors, one `std::min` call, `qsizetype` warnings found by MSVC, `QVariant` equality review | 2 to 4 days; MSVC decides the upper end |
| CI | Qt 6 on three platforms and `tidy.yml` | 1 day |
| Packaging | `windeployqt` options and DLL names, macOS trimming, AppImage checks, `tools/release`, notices | 2 to 3 days |
| High-DPI and look | Windows and Linux at 100 %, 125 % and 150 %, network view, diagrams, dialogs | 1 to 2 days |
| Release rehearsal | the checklist on three platforms | 1 day |

These are estimates for one developer who knows the repository, about 8 to 12 days in all. Replacing Qt Charts is not
included.

## Risks

- Qt Charts is deprecated since Qt 6.10 and Qt Graphs is named for new projects. Qt Graphs is a QML module, so it would
  likely bring back the Qt Quick and QML frameworks that #488 removed.
- MSVC narrowing warnings are unknown until a Qt 6 Windows build runs; with warnings as errors they block CI.
- `QList` reference invalidation: only the tests and the simulation identity check can find it.
- Fractional scaling on Windows and Linux.
- A larger macOS bundle with Homebrew Qt 6.
- Following new minor releases instead of one LTS.

## Recommended order of work

1. **Build with versionless Qt targets.** Find Qt 6 or fall back to Qt 5.15 in `CMakeLists.txt` and use `Qt::`
   targets, which Qt 5.15 already has ([Qt 5 and Qt 6 compatibility](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html)).
   Qt 5 stays the default; no behaviour changes.
2. **Make the sources compile with Qt 6.** Guard the Qt Charts namespace lines and the `QNativeGestureEvent` calls and
   test constructors with a `QT_VERSION` check, use the six-argument `QMouseEvent` constructor in the tests and cast
   the size in `TelemetrySender.cpp:491`. Every change also builds with Qt 5.15.
3. **Add a Qt 6 build to CI.** One job per platform with Qt 6 next to the Qt 5 jobs, not required at first. Fix the
   MSVC `qsizetype` warnings it reports, then make it required.
4. **Package with Qt 6.** Update `windeployqt` options, DLL names, the macOS trimming, the AppImage checks,
   `tools/release` and `THIRD_PARTY_NOTICES.md`. Compare package sizes and contents with the Qt 5 packages.
5. **Check high-DPI rendering and the stylesheet.** Review the network view, the diagrams and the dialogs on Windows
   and Linux at 125 % and 150 %, light and dark. Fix what fractional scaling breaks.
6. **Switch the releases to Qt 6 and remove Qt 5.** Make Qt 6 the release build before 2027-05-19, then remove the Qt 5
   jobs, `util/QtMsvcCompat.h` and the Qt 5 notes. Rehearse a release on all three platforms.
7. **Decide on Qt Charts.** Weigh Qt Graphs against keeping Qt Charts while Qt ships it. Include the size of the Qt
   Quick runtime in the decision.

Status on 2026-10-19: steps 1 and 2 are done. For step 3 CI has Qt 6 jobs that are not required yet. With Qt 6.8.3 on
macOS the application and all tests build with warnings as errors. On the offscreen platform these tests fail, and the
Qt 6 jobs leave them out:

- `test_creator_acceptance_smoke`, `test_dialog_presentation_contract` and `test_editor_smoke` passed their checks,
  then crashed when `MainWindow` deleted the infrastructure panel: refilling the table after a facet switch let the
  table open an editor it lost track of. Fixed on 2026-10-20: no editor opens while the table is refilled.
- `test_visual_polish_smoke_live` crashes while the network view paints, in the BSP index of the scene.
- On Windows, `test_openrequestrelay` finds that a second launch no longer hands its requests to the running
  instance after the first few.
