# CI and release

This page describes the workflows that check pull requests, `main` and the release branches, and the
scripts that build, sign and check the release packages.

## CI and release branches

- `main` is the validation branch. Pull requests build the project and run
  the CTest suite without the tests labelled `network` (those run on Windows only), in parallel, on macOS, Windows (MSVC, vcpkg
  x64) and Linux (Ubuntu 24.04), each with Qt 6.8.3, unless every changed file matches the
  documentation rule below. The suite holds the end-to-end smokes of the
  committed scenes (see [Simulation Smoke Test](smoke-tests.md#simulation-smoke-test) and
  [Scene Roundtrip Smoke Test](smoke-tests.md#scene-roundtrip-smoke-test)). The `docs` job runs
  `tools/docs/check_docs.py` on every pull request, documentation-only ones
  included. The same checks run on `main` once a night (02:17 UTC)
  and when started by hand from the Actions tab (`workflow_dispatch`); both
  always build, and both also start the [leak check](smoke-tests.md#leak-check) and the [nightly sanitizer runs](smoke-tests.md#nightly-sanitizer-runs) on Linux.
  A push to `main` starts no run. A scheduled workflow runs
  on the default branch only, and GitHub pauses schedules in a repository
  without activity for 60 days. The three legs run independently
  (`fail-fast: false`) and follow the same rule. A newer push to a pull
  request cancels its running checks; a scheduled or manual run is never
  cancelled. After the tests, the Windows leg also makes the request of the
  [HTTPS update check](testing.md#https-update-check) in a step of its own. A failed leg
  uploads `ctest.log` and the GUI autostart log as an artifact named after the
  leg. Do not make any of these legs a required check; `main` requires the
  result checks described below.
- The format check (`format.yml`) runs `tools/format.py` for a pull request to
  `main` that changes `EGTRAIN/QEGTRAIN/`, `.clang-format`, `.clang-format-ignore`,
  `tools/format.py` or `format.yml`, and when started by hand.
- The tidy check (`tidy.yml`) runs `tools/tidy.py` for a pull request to `main` that
  changes `EGTRAIN/QEGTRAIN/`, the root `CMakeLists.txt`, `.clang-tidy`, `tools/tidy.py` or
  `tidy.yml`. A pull request analyses the changed sources and the sources that include a
  changed header; the nightly run (02:37 UTC) and a manual start analyse every source. The
  run installs clang-tidy 23.1 from apt.llvm.org and lists the findings; the local command
  and the checks are described in the [coding guidelines](coding-guidelines.md#static-checks).
  The result check is named `Tidy result`.
- The documentation workflow (`docs.yml`) builds the [documentation site](testing.md#documentation-site)
  for every pull request, fails a pull request that adds undocumented classes or members to the
  files it changes, and publishes the site to `gh-pages`: `dev/` from `main`, and `<version>/`
  and `latest/` after a successful release.
- A pull request to `main` that changes a packaging input also runs the
  package check (`package-check.yml`). The inputs are the workflow files
  `package-check.yml`, `package.yml` and `release.yml`, `tools/release/`, the vcpkg manifest in `tools/vcpkg/`,
  `installer/`, `Info.plist.in`, `EGTRAIN/QEGTRAIN/update/` and
  `EGTRAIN/QEGTRAIN/app/main.cpp`. The check also runs once a night on `main`
  and can be started by hand, which covers changes that reach the packages
  through other files, such as the root `CMakeLists.txt`. The check selects the version as a release run that does not publish does (the CMake
  baseline, through `tools/release/version.py`) and calls the reusable workflow
  `package.yml`, as the release workflow does. A green check proves that the macOS, Windows and Linux packages
  build, pass the completeness checks of their jobs and are uploaded as
  artifacts of the run. The Windows package job also fails when the software
  OpenGL, Direct3D or DXC compiler libraries, Qt Quick, QML or virtual
  keyboard files or `vc_redist.x64.exe` are in the package or when a program
  file imports a DLL that is neither in the package nor part of Windows, starts
  a copy of the package with
  `tools/release/package_start_smoke.py`, makes the request of the
  [HTTPS update check](testing.md#https-update-check) from that copy, and prints the number of files and
  bytes of the package in its job summary. It also prints the signature state of
  `QEGTRAIN.exe`, `egtrain_update_helper.exe` and `scene_tool.exe` with
  `tools/release/verify_signatures.ps1 -Report`, which fails for no file. A push
  to `production` or a `v*` tag runs the job `sign-windows` when the repository
  variable `WINDOWS_SIGNING` is `signpath`; it signs those three files and
  replaces the uploaded directory with one that holds them (see
  [Code signing](code-signing.md#signing-in-ci)). Every other run skips it. The
  Windows finalize job (`finalize-windows`) then downloads that directory, compiles
  `installer/egtrain.iss` with Inno Setup (the copy on the runner image, or a
  pinned version from Chocolatey when the image has none; the script needs 6.3
  or newer) into `EGTRAIN-Setup-<version>-x64.exe`,
  runs `tools/release/installer_smoke.ps1` on it (see
  [Installer smoke](#installer-smoke)), writes `EGTRAIN-Portable-<version>-x64.zip`
  from the same directory with `build_release_assets.py --payload`, extracts that
  archive and starts the extracted program with the same start script, and
  uploads the installer and the archive as the artifacts
  `EGTRAIN-windows-x64-installer` and `EGTRAIN-windows-x64-portable`. The smoke
  needs an installer of a higher version for its upgrade check, so the job
  compiles a second one with the next patch version and uploads only the first.
  When the job fails, it uploads the logs of Setup and of the uninstaller as the
  artifact `finalize-windows-diagnostics`. When `sign-windows` succeeded, the job
  `sign-windows-installer` then sends the uploaded installer to the signing
  service, requires valid signatures on the signed installer and on the three
  program files of the payload, and only then replaces the installer artifact
  with the signed file. The installer smoke runs on the installer before it is
  signed. The macOS package job builds for
  macOS 26 (`-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0`) and writes the minimum system
  version of the executable into the `LSMinimumSystemVersion` key of the property
  list, so the app needs macOS 26 or later. It copies `scene_tool` into the app, to
  `Contents/MacOS/scene_tool`, puts `LICENSE` and
  `THIRD_PARTY_NOTICES.md` in `Contents/Resources`, and removes the virtual keyboard
  input plugin with the Quick and QML frameworks, and the WebP and TIFF plugins
  with their libraries, from the app. All of this comes before the one
  signature, which `tools/release/sign_macos.sh` makes (see
  [macOS signing script](#macos-signing-script)). After the signature the job fails when one of the removed files is in
  the app, runs `scene_tool` on the Paimpol scene of the app without
  the GUI, starts a copy of the app with the same script, and prints the number
  of files and bytes of the app before and after the removal in its job summary.
  The steps that fill, trim and check the app call `tools/release/package_macos.sh` (see
  [macOS packaging script](#macos-packaging-script)).
  In a push to `production` or a `v*` tag it then notarizes and staples the app
  when the environment holds notarization credentials (see
  [macOS notarization script](#macos-notarization-script)). It builds
  `EGTRAIN-<version>-macOS-arm64.dmg` from the app with
  `tools/release/make_dmg.py --layout`, and no later step changes the app. With
  a Developer ID identity the image is signed, and with notarization credentials
  it is notarized and stapled. The
  job mounts the image read-only and detaches it on every path. It checks that
  the volume holds only `QEGTRAIN.app`, the link `Applications` and the hidden
  file `.DS_Store`, that this file equals `tools/release/dmg/DS_Store`, that the
  app in the image and a `ditto` copy of it pass
  `codesign --verify --deep --strict`, that the property list of the app in the
  image declares `.egscene` as its one document type
  (`CFBundleDocumentTypes`) and exports the type `com.egtrain.simulator.egscene`,
  which conforms to `public.data` only (`UTExportedTypeDeclarations`), that the
  copy has the version and the identifier of the build, its programs and the
  guide, and that the copy runs Paimpol without the GUI to `End of Simulation`.
  The image is the only file of the artifact `QEGTRAIN-macos-arm64`; no zip is
  made. The Linux package job extracts its AppImage and starts the extracted
  image with the same script on a virtual display, after the script has checked
  that every Qt and ZeroMQ library of the programs and plugins comes from the
  image. The start in each of the three package jobs is the headless run and the
  window start, with no CSV or PNG export, because a release build has no test
  hooks to ask for one. The check does not show how the SVG icons look, typing
  into text fields, the native file dialogs, how Finder presents the disk
  image, whether a double-click on a `.egscene` file in Finder opens EGTRAIN, or
  a start on a machine without Qt or a display server; the clean-install
  rehearsal in the
  [release testing checklist](release-testing-checklist.md) covers them. After
  the three packages are built, the job `release-assets` downloads the
  artifacts and runs `tools/release/build_release_assets.py` on them with the
  command line of the release job
  (`--artifacts artifacts --output release-assets`). The Windows package
  arrives as a directory with `QEGTRAIN.exe` at its top level. A green
  check therefore also proves that the script accepts the real packages, the
  installer and the portable archive, that the archive holds the files of the
  payload with the same bytes, that the script builds a manifest that the
  application accepts by its rules for the file list and the package sizes, and
  that the assets it writes are exactly the files that a release publishes.
  It does not prove a release: nothing is signed with real credentials (the
  macOS bundle is signed ad hoc, as in a release without a certificate), nothing is
  published, and the check has read permission only. It does not prove the
  release job either: that job runs only on a push to `production` or a `v*`
  tag, so its steps (the script step, the glob that publishes
  `release-assets/*`, the creation of the release) are first run by a release.
  The finalize job extracts the archive with Python, not with the PowerShell
  command that the application uses; only the unit test of the script extracts an
  archive with that command, on the Windows leg of the CMake workflow. The
  runner account is an administrator, so the check does not show that a
  standard user installs without an elevation request. It runs on a machine
  with the Visual C++ runtime, so it does not show that the package starts on a
  machine without it. It does not show how Windows presents the installer (for
  an unsigned download, the SmartScreen screen), the Start Menu entry or the
  entry in the list of installed apps. The smoke reads the registry keys of the
  `.egscene` file type but opens no file, so it does not show that a double-click
  on a `.egscene` file in Explorer opens EGTRAIN. The
  installer and portable rows of the
  [release testing checklist](release-testing-checklist.md#windows-installer-and-portable-archive)
  cover the rest on a Windows machine. A release still runs only from
  `release.yml`. A newer push to the pull request cancels the running check.
- `production` is the release branch. Its full pipeline packages macOS,
  Windows, and Linux applications, runs CTest, which holds the smoke suite,
  and the sanitizers, validates the scene bundles, and publishes a stable `vX.Y.Z`
  release. The three package jobs and the Rehearse Linux job, which runs the
  clean-install steps on the Linux package, are in `.github/workflows/package.yml`, which
  `release.yml` calls from its `package` job with the selected version. The
  Windows package job uploads the assembled directory as the artifact
  `QEGTRAIN-windows-x64-payload`, and the Windows finalize job builds the
  installer and the portable archive from it and uploads them as
  `EGTRAIN-windows-x64-installer` and `EGTRAIN-windows-x64-portable`. The release
  job builds the files of the release from the downloaded artifacts with
  `tools/release/build_release_assets.py` and publishes the files it writes; the
  portable archive is written only by the script. Before
  building, the pipeline increments the highest patch version
  among the CMake baseline, existing stable tags, and reserved release versions.
  Every build job, the package metadata and the update manifest use that same version. Local builds use the
  baseline unless configured with `-DEGTRAIN_VERSION=X.Y.Z`.
  `update-manifest.json` has one entry per distribution key under `assets`, each with
  `name`, `sha256` and `size`. The running copy picks its entry by key and downloads
  the asset that `name` gives. The keys are `windows-x64` (portable package), `windows-x64-installer`,
  `macos-arm64` and `linux-x86_64`. A Windows copy with `installed-by-setup.ini`
  next to `QEGTRAIN.exe` is installer-managed (`currentDistribution` in
  `update/Distribution.cpp`) and takes `windows-x64-installer`; every other copy
  takes the key of its platform, which for a portable Windows copy is `windows-x64`.
  An installer-managed copy does not update itself by replacing files: it downloads
  the installer and the update helper runs it. The installer is the only intended
  source of that file, and the package payload and the portable archive must not
  contain it.
  The updater accepts a `name` only when it
  fits a pattern of its key, where `<version>` is the version of the manifest,
  which must equal the release tag without its leading `v`. The same patterns
  decide which release assets the update check keeps (`kAssetPatterns` in
  `update/ReleaseInfo.cpp`). The table lists the names a release may use; a name
  outside it needs a new application:

  | Key | Accepted names |
  | --- | --- |
  | `windows-x64` | `QEGTRAIN-windows-x64.zip`, `EGTRAIN-Portable-<version>-x64.zip` |
  | `windows-x64-installer` | `EGTRAIN-Setup-<version>-x64.exe` |
  | `macos-arm64` | `QEGTRAIN-macos-arm64.zip`, `EGTRAIN-<version>-macOS-arm64.dmg` |
  | `linux-x86_64` | `QEGTRAIN-linux-x86_64.AppImage` |

  A name with a path separator, another version or another kind of file is
  rejected. The manifest has no field for the kind of package; the key and the
  pattern decide it. The manifest of a published release keeps working because
  its names are in the table. Staging handles the archives, the macOS disk image
  and the AppImage. The installer is checked against the manifest and staged as
  it is, without extraction. A package
  whose name ends in `.dmg` is mounted read-only below the staging folder with
  `hdiutil attach -readonly -nobrowse -noautoopen`, `QEGTRAIN.app` is copied from
  the root of the volume with `ditto`, and the volume is detached on every path
  (by force on the last attempt). The zip package of earlier releases, which
  holds `QEGTRAIN-Lebanon/QEGTRAIN.app`, can still be staged.
  `update-manifest.json` also lists the files of the Windows package. Before
  the updater replaces an installation, it requires every listed file and a
  fixed set of runtime files (`requiredRuntimeFiles()` in
  `update/WindowsStaging.h`) in the extracted package. Keep that set in step
  with the package verification in the Windows package job.
- Production and tag releases run serially. A stale production run cannot
  publish after the branch advances, and an existing production release tag
  cannot be overwritten. If a failed-job retry encounters a used version,
  rerun all jobs on the latest production commit to allocate a new version.
- `v*` tags supply their application version explicitly. Tags containing a
  prerelease suffix remain prereleases and are not offered by the updater.
  `workflow_dispatch` validates the pipeline without publishing.
- Releases remain drafts until every package and scene bundle is uploaded.
  Stable production releases then appear in the application's update checks.

Pushes and pull requests to `production` skip the release workflow when changes
are limited to Markdown files (`**.md`), `docs/`, the root `LICENSE`, or
`.github/ISSUE_TEMPLATE/`. Mixed changes still run the full workflow, as do
changes to source, tests, scenes, build settings, or workflows.
Documentation-only production pushes do not publish a new release. Updated
packaged guides ship with the next release; `v*` tags and manual release runs
are not filtered by changed paths.

### Required checks on `main`

Every pull request to `main` starts the CMake, Format and Package check
workflows; their triggers have no path filter. The first job of each, `changes`,
compares the files of the pull request with the rule of the workflow, and the
real jobs skip when the rule selects nothing:

- CMake builds unless every changed file is a Markdown file (`**.md`), under
  `docs/` or `.github/ISSUE_TEMPLATE/`, or the root `LICENSE`. Its `docs` job
  checks the documentation on every pull request.
- Format runs for the files of the format check, listed above.
- Tidy runs for the files of the tidy check, listed above.
- The package check runs its jobs for the packaging inputs, listed above.

A nightly or manual run always does its jobs. The condition of a job is
evaluated before its matrix is expanded, so a skipped `build` job would be
listed as `build (${{ matrix.name }})`. The three legs of the CMake workflow
therefore always start, and their steps skip.

The last job of each workflow reports one check: `CMake result`, `Format result`,
`Tidy result` or `Package check result`. It runs whatever the other jobs did, fails when any of
them failed or was cancelled, and succeeds when they succeeded or were skipped.
A documentation-only pull request gets four successful result checks without a
build when the documentation check finds nothing. `main` requires these four checks and no others, so a pull request can
merge automatically when they are green. Do not require the build legs, the
format job, the tidy job or the package jobs: a required check that no run reports under its
own name leaves the pull request waiting forever.

### Release assets script

`tools/release/build_release_assets.py` builds the files that a release
publishes from the artifacts of the package jobs, and writes the portable
Windows archive for the Windows finalize job:

```bash
python3 tools/release/build_release_assets.py --version X.Y.Z --artifacts DIR --output DIR
python3 tools/release/build_release_assets.py --version X.Y.Z --payload DIR --output DIR
```

`--artifacts` holds one directory per artifact, as `actions/download-artifact`
makes them when it is given no artifact name:

- `QEGTRAIN-windows-x64-payload/`: the assembled Windows package, with
  `QEGTRAIN.exe` at its top level, uploaded by the Windows package job
- `EGTRAIN-windows-x64-portable/EGTRAIN-Portable-X.Y.Z-x64.zip`
- `EGTRAIN-windows-x64-installer/EGTRAIN-Setup-X.Y.Z-x64.exe`
- `QEGTRAIN-macos-arm64/EGTRAIN-X.Y.Z-macOS-arm64.dmg`
- `QEGTRAIN-linux-x86_64/QEGTRAIN-linux-x86_64.AppImage`
- `EGTRAIN-scenes/`: the `.egscene` files of the bundled cases

Other directories are ignored. `--output` must not exist or must be an empty
directory, and the script writes nowhere else. Afterwards it holds exactly the
files of a release: the packages named above, the `.egscene` files of the case
studies and `update-manifest.json`. The packages and the scene bundles are copies.
The `sha256` and `size` of each package in the manifest come from the file in
`--output`. The `files` of the `windows-x64` entry are the members of the
portable archive, sorted by their full path as a string (`a.b` comes before
`a/b`). The `windows-x64-installer` entry has no `files`. The script prints the
name and size of each file. The release job and the package check run it with
`--artifacts artifacts --output release-assets` on the artifacts they download,
and the release job publishes the files of the output directory.

`--payload` takes the assembled Windows package and writes
`EGTRAIN-Portable-X.Y.Z-x64.zip` into `--output`: one member per file with forward
slashes, in the order of the sorted file list, no directory entries and no
top-level folder. The Windows finalize job runs it on the directory that it also
gives to Inno Setup, so the installer and the archive hold the same files. The
`--artifacts` form copies the archive and checks that it has exactly the members
of the payload artifact, in that order, with the same bytes.

Every input is checked before `--output` is created:

- the version has the form `X.Y.Z` that `tools/release/version.py` accepts;
- each file above exists and is not empty, the artifact directories hold no other
  file, and the payload holds regular files and directories only (a link is an
  error, and so is a directory that cannot be read);
- the portable archive is a readable archive that holds the files of the payload
  and nothing else, with the same bytes;
- the file list of the Windows package has the rules of `parseManifestFiles` in
  `update/ReleaseInfo.cpp`: at most 4096 entries, each path at most 260 UTF-16
  code units, no `:` and no NUL, no empty, `.` or `..` segment, and
  `QEGTRAIN.exe` in the list. A backslash is also rejected, where the
  application would read it as a slash, because the list names the members of
  an archive that uses forward slashes only;
- each package is between 1 byte and 2 GiB, the limit of `parseUpdateManifest`
  in `update/ReleaseInfo.cpp`. With `--payload` the size of the archive is
  checked once it is written.

The script does not check the fixed runtime file set (`requiredRuntimeFiles()`
in `update/WindowsStaging.h`): the Windows package job verifies it before it
uploads the package.

The test is `tools/release/test_release_assets.py`. It builds a small artifact
tree, with the portable archive written by the script, runs the script on it and
checks the rules directly. Run it with
`ctest --test-dir build -R test_release_assets --output-on-failure`.

### Disk image script

`tools/release/make_dmg.py` builds the macOS disk image from a finished
`QEGTRAIN.app`:

```bash
python3 tools/release/make_dmg.py --version X.Y.Z --app PATH/QEGTRAIN.app --output DIR [--layout]
```

The image is `DIR/EGTRAIN-<version>-macOS-arm64.dmg`, one of the names in the
table of accepted names above. It is a compressed, read-only (UDZO) HFS+ image
with the volume name `EGTRAIN`. The volume holds `QEGTRAIN.app` and
`Applications`, a symbolic link to `/Applications` to drag the app onto, and
nothing else: no scene folder, no `.egscene` file, no `scene_tool` and no guide.
The script copies the bundle with `ditto`, as the package job does, and does not
change it afterwards, so a signed app keeps a valid signature. The script prints
the name and size of the image.

With `--layout` the volume also holds the hidden file `.DS_Store`, a copy of
`tools/release/dmg/DS_Store`. It tells Finder to show the volume as an icon view
of 600 by 360 points with icons of 128 points and without toolbar, sidebar, path
bar and status bar, with `QEGTRAIN.app` on the left and `Applications` on the right
at the same height. There is no background picture and no volume icon. Without
`--layout` Finder places the two icons itself. The file is written without
Finder by `tools/release/dmg/make_ds_store.py`, which needs the `ds_store`
package. The package is not a dependency of the repository or of CI, so run the
script in a throwaway environment and commit the result:

```bash
uv run --no-project --with ds_store python tools/release/dmg/make_ds_store.py tools/release/dmg/DS_Store
```

The sizes and positions are constants at the top of that script. How the window
looks is checked by opening the image on a Mac.

These checks run before the script creates anything:

- the version has the form `X.Y.Z` that `tools/release/version.py` accepts;
- `--app` is a directory named `QEGTRAIN.app` that holds `Contents/Info.plist`
  and the directory `Contents/MacOS`;
- `--output` is not a file, and the image does not exist in it yet. The script
  never overwrites a file;
- with `--layout`, `tools/release/dmg/DS_Store` is a file;
- `ditto` and `hdiutil` are on the path. Both are macOS tools, so the script
  runs only on macOS.

Then it creates `--output` if needed, stages the app, the link and, with
`--layout`, `.DS_Store` in a temporary folder that it removes on every exit, and
runs `hdiutil create` on the folder.
Each tool gets 120 seconds, and only its exit status decides whether it worked:
`hdiutil` prints deprecation notices on standard error and still exits with 0.
When `hdiutil` fails or times out, the script removes the partly written image
and exits with the command and its error text.

Two images made from the same bundle have the same file name, volume name and
contents. The script does not promise equal image bytes. It sets no background
picture, volume icon or licence text, and it does not sign, notarize or upload
anything. The macOS package job in `package.yml` calls it once the app is signed
and checked, with `--output build/dmg --layout`, and uploads the image as the
artifact `QEGTRAIN-macos-arm64`.

The test is `tools/release/test_make_dmg.py`. On macOS it signs a small fixture
bundle (a program that exits with 0, a data file and a symbolic link inside the
bundle), builds the image and mounts it read-only below a temporary folder. It
checks the file name, the format, the volume name, the entries at the top of the
volume, that no `.egscene` file or `Scenes` folder outside the app is on the
volume, and that the app in the image equals the fixture in its paths, file
bytes, link targets and permission bits. It also checks that
`codesign --verify --deep --strict` passes on the app in the image and on a
`ditto` copy taken from it, and that the copy runs. It builds a second image
with `--layout` and checks that its `.DS_Store` equals the committed file and
that the app in it is unchanged. On every platform it checks the file name
against `kAssetPatterns` in `update/ReleaseInfo.cpp`, the input rules, the
message for a missing tool, and the command lines, the link, the staged
`.DS_Store` and the handling of a failing or timed-out command, with stand-ins
for `ditto` and `hdiutil`, and that the committed layout file names the app and
the link. Run it with
`ctest --test-dir build -R test_make_dmg --output-on-failure`.

### macOS packaging script

`tools/release/package_macos.sh <step> <app>` runs one step of the macOS package job on the app in the build folder,
`build/QEGTRAIN.app` in the job. Each step of the job is one call:

| Step | Job step | What it does |
| --- | --- | --- |
| `bundle` | Bundle Qt and dependencies into the app | Runs `macdeployqt` from `$QT_ROOT_DIR` on the app and copies `egtrain_update_helper` of the build folder into `Contents/MacOS`. A Homebrew library can name a dependency as `@rpath/<name>`, which `macdeployqt` does not copy: the step copies it from the Homebrew prefix and points the reference at the copy, until nothing is missing. |
| `add-files` | Add scene_tool and the licence files to the app | Copies `scene_tool` of the build folder into `Contents/MacOS`, and `LICENSE` and `THIRD_PARTY_NOTICES.md` into `Contents/Resources`. |
| `trim` | Remove unused Qt modules and plugins | Removes the virtual keyboard and input context plugins, the Quick and QML frameworks, the WebP and TIFF plugins with their libraries and the database drivers other than SQLite, and prints the number of files and bytes of the app before and after. |
| `thin` | Keep only the arm64 code of the Qt 6 libraries | Keeps the arm64 code of every universal file. |
| `strip-paths` | Remove build machine paths | Runs `tools/release/macos_build_paths.py fix`. |
| `min-version` | Set the minimum macOS version | Writes the minimum system version of the executable into `LSMinimumSystemVersion`. |
| `verify-contents` | Verify the app is self-contained | Requires the Qt frameworks, the cocoa platform plugin, the SVG plugins, the macOS style, the SQLite driver, the icon and ZeroMQ, fails on a removed file and on a build machine path (`macos_build_paths.py check`), and runs `codesign --verify --deep --strict`. |
| `verify-signature` | Verify the signature | Prints the identifier, the authority and the team, and requires an ad-hoc signature without `MACOS_SIGNING_IDENTITY` and a Developer ID Application authority, a team, the hardened runtime and a timestamp with it. |
| `verify-package` | Verify the packaged app | Requires the three programs, the licence files, the manual and the Paimpol scene, and runs `scene_tool validate` on that scene. |

The first six steps change the app, so they come before the signature, and the last three check the signed app. The app
first runs in the next step, **Start the package**, from a copy. `bundle` and `add-files` take the programs from the
folder that holds the app, which is the build folder. `bundle` needs `QT_ROOT_DIR` and Homebrew. `trim` and
`min-version` also write their lines into the job summary when `GITHUB_STEP_SUMMARY` is set, so every step also runs
outside a workflow. To package a local build, copy the app and the two programs into a new folder and run the steps in
the order of the table, with `tools/release/sign_macos.sh` between `min-version` and `verify-contents`:

```bash
mkdir -p /tmp/package/build
ditto build/QEGTRAIN.app /tmp/package/build/QEGTRAIN.app
cp build/scene_tool build/egtrain_update_helper /tmp/package/build/
QT_ROOT_DIR="$HOME/Qt/6.8.3/macos" bash tools/release/package_macos.sh bundle /tmp/package/build/QEGTRAIN.app
```

A build without the Sphinx environment of the manual has no `EGTRAIN.qch`, and `verify-package` then fails.

The test is `tools/release/test_package_macos.sh`. It builds a fixture build folder whose app holds what `macdeployqt`
leaves: frameworks, plugins and libraries, among them the files that `trim` removes, a universal program built for
macOS 26 with a run path into the Homebrew Cellar, and a `scene_tool` that fails on request. It runs every step but
`bundle` in the order of the job, signs the app ad hoc with `sign_macos.sh` in between, and checks the result of each:
the copied files, the removed and the kept files, the size lines in a job summary and a run without one, the arm64
program, the removed run path, the minimum version, and the three checks. Then it requires `verify-contents` to fail
for a missing SVG icon engine, a missing Qt Svg framework, a WebP plugin, a run path into the Homebrew Cellar and a file
changed after the signature, `verify-signature` to fail when an identity is set and the signature is ad hoc, and
`verify-package` to fail for a missing manual and a scene that does not validate. It does not run `bundle`, which needs
`macdeployqt`, Qt and Homebrew, or sign with an identity. Run it with
`ctest --test-dir build -R test_package_macos --output-on-failure`.

### macOS signing script

`tools/release/sign_macos.sh <app>` signs the app in the macOS package job
(step **Sign the app**). Without `MACOS_SIGNING_IDENTITY`, or with it empty, it
runs `codesign --force --deep --sign -`, an ad-hoc signature that carries no
publisher. With the variable set to a Developer ID Application identity from a
keychain on the search list, it signs each Mach-O file and each nested
`.framework`, `.app` and `.xpc` bundle on its own, with `--options runtime
--timestamp`: first those in `Contents/Frameworks`, the deepest first, then
those in `Contents/PlugIns`, then the programs in `Contents/MacOS`
(`egtrain_update_helper` and `scene_tool`), then the main executable, and
finally the bundle, without `--deep`. It stops before it signs anything when a
Mach-O file is somewhere else in the bundle, and it prints no credential.

In the workflow, a push to `production` or a `v*` tag runs the job in the
environment `macos-signing`. The step **Import the signing certificate** reads
the environment secrets `MACOS_CERTIFICATE` (a base64 encoded `.p12` file with
the private key) and `MACOS_CERTIFICATE_PASSWORD`, and the variable
`MACOS_SIGNING_IDENTITY` (the name of the identity, as `security find-identity`
prints it). It imports them into a keychain of its own, passes the identity to
the later steps, and the last step of the job deletes the keychain. Without a
certificate the step does nothing and the app keeps the ad-hoc signature. The
step **Verify the signature** prints the identifier, authority and team of the
app, and requires an ad-hoc signature without an identity and a Developer ID
Application authority, a team, the hardened runtime and a timestamp with one.
Neither the workflows nor the release scripts remove the quarantine attribute or
turn Gatekeeper off; `test_ci_workflow` checks it.

The test is `tools/release/test_sign_macos.sh`. It builds a fixture bundle with a
framework, a flat library, a plug-in, two programs next to the main one and a
data file, and runs the script with a `codesign` in front of the real one that
records its arguments and turns `--timestamp` into `--timestamp=none`, because a
secure timestamp needs Apple's server. It checks that the ad-hoc path makes one
`--deep` call and that the bundle verifies with
`codesign --verify --deep --strict`. It runs the identity path with the identity
`-`, so that the real tool signs without a certificate, and compares the calls
with the expected order and options, checks that the bundle verifies and that
every piece of code has the hardened runtime. A recording that signs nothing
shows that an identity name with spaces and parentheses reaches each call as one
argument and that no credential in the environment is printed. It also checks
that Mach-O code in `Contents/Resources` stops the script and that a missing app
is refused. The test makes no certificate or keychain, so it does not show that
a Developer ID certificate signs, that the timestamp server answers or that
Apple accepts the app. Run it with
`ctest --test-dir build -R test_sign_macos --output-on-failure`.

### macOS notarization script

`tools/release/notarize_macos.sh <app or disk image>` notarizes a file that is
signed with a Developer ID Application identity. It stops when the signature is
not one, sends the file to Apple with `xcrun notarytool submit --wait` (an app
as a zip archive made with `ditto`, an image as it is) and requires the verdict
`Accepted`. For any other verdict it prints Apple's log of the submission and
fails. Then it staples the ticket with `xcrun stapler staple`, tried up to five
times because Apple may not hand out the ticket at once, and checks it with
`xcrun stapler validate` and `spctl --assess` (`--type execute` for an app,
`--type open --context context:primary-signature` for an image). Any failing
command fails the script.

The credentials come from the environment and nowhere else. The script uses
either an App Store Connect API key, with `MACOS_NOTARY_KEY` (the text of the
`.p8` file), `MACOS_NOTARY_KEY_ID` and `MACOS_NOTARY_ISSUER_ID`, or an Apple ID,
with `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID` and `MACOS_NOTARY_PASSWORD`
(an app-specific password). The API key wins when both are complete. With none
of the six set, or all of them empty, the script says that it skipped the file
and succeeds, so the file is not notarized. With some set and neither group
complete it fails and names the missing variables. It prints no value, and it
writes the key to a file in a private temporary folder that it removes on every
exit.

In the workflow, the credentials belong to the environment `macos-signing`: the
key and the password are environment secrets, the other four are environment
variables. Two steps read them and nothing else does. **Notarize the app** runs
after **Start the package** and before **Build the disk image**, so the app in
the image already carries its ticket and keeps it when it is copied out of the
image offline. After the image is built, **Sign the disk image** signs it with
the identity of the import step (`codesign --force --timestamp --sign`), or
leaves it unsigned when there is no identity, and **Notarize the disk image**
sends it as a second submission. Both notarization steps run only for a push to
`production` or a `v*` tag, have a time limit of 60 minutes, and have no
condition on a credential and no `continue-on-error`: a release job without
credentials succeeds and ships files that are not notarized, and one with
credentials fails when any command of the script fails, so a file that Apple
rejected is never published as notarized. **Verify the disk image** then checks
the signature of the app in the finished image as before.

The test is `tools/release/test_notarize_macos.sh`. It puts stand-ins for
`codesign`, `xcrun` and `spctl` in front of the real tools, so nothing reaches
Apple, and uses the real `ditto`. It checks the arguments and the order of the
calls for an app with an API key and for an image with an Apple ID, that the app
goes to `notarytool` as an archive that holds it and the image as it is, that the
key file has the mode 600 and is gone afterwards, that the script skips without
credentials and fails for partial ones, wrong calls, a file without a Developer
ID signature, a verdict of `Invalid`, no verdict, a failing `notarytool`, a
ticket that `stapler` hands out only at the third attempt or never, a ticket that
does not validate and a refusal by Gatekeeper, and that no value of a credential
reaches the output. It runs on macOS only. The package job runs it in the step
**Test the notarization script**, and it is not registered in CTest. It does not
show that Apple accepts the app, that the ticket attaches to the files or that
Gatekeeper accepts them: the first run with real credentials is the first proof of
that. Run the test with `bash tools/release/test_notarize_macos.sh`.

### Installer script

`installer/egtrain.iss` is the Inno Setup script of the Windows installer.
Compile it on Windows with Inno Setup 6.3 or newer:

```text
ISCC.exe /DVersion=X.Y.Z /DPayload=<package directory> [/O<output directory>] installer\egtrain.iss
```

`/DVersion` is the release version. `/DPayload` is the absolute path of the
assembled Windows package, the directory that the Windows package job uploads as
`QEGTRAIN-windows-x64-payload`. Setup writes `EGTRAIN-Setup-<version>-x64.exe`,
the name in the table of accepted names above, to `installer/Output` or to the
`/O` directory. [Windows installer script](windows-installer.md) describes the
parameters, what the installer does and the Setup switches for a silent install.

The installer is for the current user and needs no administrator rights. It
installs the package directory as it is into `%LOCALAPPDATA%\Programs\EGTRAIN`
by default, adds a Start Menu entry and an optional desktop shortcut, and
registers an uninstall entry under the current user. It also associates the
`.egscene` file type with `{app}\QEGTRAIN.exe` for the current user, which the
uninstaller removes. It replaces the `Scenes` folder of the installation folder
on every install and runs no program. Setup shows the GPL-3.0 text of `LICENSE`
on a licence page, and the Windows package job copies `LICENSE` into the
package, so the installed folder and the portable archive hold it. The
Windows finalize job of `package.yml` compiles the script, and
`tools/release/installer_smoke.ps1` installs the result on the runner.

The test is `tools/release/test_installer_script.py`. It reads the script,
`installer/installed-by-setup.ini`, `docs/development/windows-installer.md`, `kAssetPatterns`
in `update/ReleaseInfo.cpp`, `LICENSE` and the Windows package job of
`.github/workflows/package.yml` as text, looks for the icon file, and checks
that the script names `LICENSE` as its licence file and the package job copies
and checks it, and that:

- the script is ASCII and names neither `HKEY_LOCAL_MACHINE` nor a parent folder;
- it has only the sections `Setup`, `Tasks`, `Files`, `InstallDelete`, `Icons`
  and `Registry`, so no code, run or uninstall-delete section, and uses only
  the constants `{app}`, `{userpf}`, `{userprograms}` and `{userdesktop}`, so
  nothing is installed for all users, and only the two shortcuts lie outside
  the installation folder;
- the preprocessor lines are the `#ifndef` and `#error` guards for the two
  parameters, and the only macros are `{#Version}` and `{#Payload}`;
- the `[Setup]` directives that decide the scope have their pinned values: the
  `AppId`, the per-user privilege level with no override, the per-user folder,
  the 64-bit architecture, closing a running program and the refresh of the
  file associations (`ChangesAssociations`). The directives
  `PrivilegesRequiredOverridesAllowed`, `UninstallFilesDir`,
  `CreateUninstallRegKey` and `Uninstallable` are refused, and the icon file
  exists;
- the output name is one that `kAssetPatterns` accepts for
  `windows-x64-installer`;
- the package is installed as it is, with no external or downloaded file, and
  the marker file is installed next to the program and is not empty;
- only `{app}\Scenes` is deleted before an install;
- the desktop shortcut belongs to a task that is unchecked by default, and the
  only shortcuts are the two that run `{app}\QEGTRAIN.exe`;
- the `[Registry]` entries are the four of the `.egscene` file type, all for
  the current user under `Software\Classes`, and no others, so no other
  extension such as `.zip` is claimed: the default value of `.egscene` is
  `EGTRAIN.Scene` and is deleted on uninstall, with the key only when nothing
  else is left in it, so that a value of another program stays; the key
  `EGTRAIN.Scene`, its `DefaultIcon` and its `shell\open\command` are deleted
  on uninstall; the icon is the first icon of `{app}\QEGTRAIN.exe`; the open
  command is `"{app}\QEGTRAIN.exe" "%1"`;
- the README names `/DVersion`, `/DPayload` and `installed-by-setup.ini`.

The test also runs the rules on the script with CRLF line ends, as a checkout on
Windows may have them, and changes the script one line at a time to check that
each rule reports the change. It does not compile the script or run an
installer, so a green run shows nothing about Inno Setup accepting the file or
about an install on Windows. Run it with
`ctest --test-dir build -R test_installer_script --output-on-failure`.

### Installer smoke

`tools/release/installer_smoke.ps1` installs, upgrades and uninstalls the compiled
installer on Windows. The Windows finalize job runs it on the installer and on a
second installer of the next patch version, both built from the same directory:

```powershell
tools/release/installer_smoke.ps1 -Installer EXE -UpgradeInstaller EXE -Payload DIR -Version X.Y.Z -UpgradeVersion X.Y.Z [-Work DIR] [-Python NAME]
```

Run it only on a machine without EGTRAIN. It stops when an uninstall entry, a
Start Menu or desktop shortcut, `%LOCALAPPDATA%\Programs\EGTRAIN` or a key of
the `.egscene` file type (`HKCU\Software\Classes\.egscene` or
`HKCU\Software\Classes\EGTRAIN.Scene`) exists already. It changes the
registry, the Start Menu and the desktop of the current user and removes what it
installs. `-Work` must not exist or must be empty; the script puts a copy of
the package, its installation folders, a file outside them and the logs of Setup
and the uninstaller below it. It checks:

- both installers request `asInvoker` in their manifest;
- a copy of the package directory, started with `package_start_smoke.py` as a
  portable copy is, leaves no key of the `.egscene` file type in the registry;
- a silent install (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG`) into a folder
  with `/DIR` and without the desktop task leaves one uninstall entry under
  `HKEY_CURRENT_USER` that names the folder and the version, none under
  `HKEY_LOCAL_MACHINE`, a Start Menu shortcut that starts `QEGTRAIN.exe` in the
  folder, no desktop shortcut, and in the folder the files of the package with the
  same bytes, `installed-by-setup.ini` and one uninstaller, and nothing else.
  The default value of `HKCU\Software\Classes\.egscene` is `EGTRAIN.Scene`,
  and the key `EGTRAIN.Scene` has a display name, the first icon of the
  installed `QEGTRAIN.exe` and the open command `"<folder>\QEGTRAIN.exe" "%1"`;
  it is not under `HKEY_LOCAL_MACHINE`.
  `package_start_smoke.py` then starts the installed copy. The silent uninstall
  removes the entry, the shortcut, the file type and the files, and a file beside
  the folder survives. A file that the program wrote into the folder during the
  start check may stay with the folder;
- an install with `/TASKS=desktopicon` also creates the desktop shortcut, and the
  uninstall removes it. The script first plants a value of another program in
  `HKCU\Software\Classes\.egscene`. It must survive the install and the
  uninstall, which removes only the default value of that key. After the other
  uninstalls the key is gone;
- an install into the default folder, the second installer over it and an
  uninstall: the upgrade leaves one entry and one folder, the entry shows the
  higher version, a scene folder that the package does not hold is gone and the
  files are those of the package. The uninstall leaves a file outside the folder.

The script does not use `/CURRENTUSER`, because the installer script offers no
choice of scope and the switch would change nothing. The account of a CI runner
is an administrator, so a run there does not show that a standard user installs
without an elevation request. The script compares bytes with `fc.exe` and
computes no hash.

### Signature check

`tools/release/verify_signatures.ps1` checks the Authenticode signatures of the
Windows program files in a directory:

```powershell
tools/release/verify_signatures.ps1 -Directory DIR (-Require | -Report) [-ExpectedSubject SUBJECT] [-Name FILE,...] [-AllowUntrustedRoot] [-NoTimestampCheck]
```

A file passes when its status is `Valid`, the digest algorithm of its signature
is SHA-256, the signature has a timestamp and the subject of the signing
certificate equals `-ExpectedSubject`. `-Name` defaults to `QEGTRAIN.exe`,
`egtrain_update_helper.exe` and `scene_tool.exe`. `-Require` needs
`-ExpectedSubject` and exits with 1 when a file fails; `-Report` prints the
findings and exits with 0. The Windows package job runs it with `-Report` on the
unsigned package of every run, and the two signing jobs run it with `-Require`:
`sign-windows` on the payload, and `sign-windows-installer` on the signed installer
(named with `-Name`) and on the payload. The
other two switches are for tests that sign with a throwaway certificate and must
not be used on a release.

`test_verify_signatures` (`tools/release/test_verify_signatures.ps1`) runs on
Windows only. It creates a self-signed code-signing certificate in the store of
the current user, signs copies of the built `scene_tool.exe` and runs the script
on them in a new PowerShell. A copy signed with SHA-256 must pass. An unsigned
copy, a copy signed with SHA-1, a check for another subject, a missing file, a
check without `-AllowUntrustedRoot` and a check without `-NoTimestampCheck`
(the copies have no timestamp) must fail, and the output must name the reason.
The certificate is never exported or added to a trusted root, and the test
removes it with its key. Run it by hand with
`ctest --test-dir build -C Release -R test_verify_signatures --output-on-failure`.

### Configure options

The three legs of `cmake.yml` configure with the presets `ci-macos`, `ci-linux` and `ci-windows` of
`CMakePresets.json`. They inherit `ci-base`: the tests, `EGTRAIN_WARNINGS_AS_ERRORS=ON` and `EGTRAIN_IPO=OFF`, in
`build/`, where the later steps look. The configure step of each leg adds the compiler launcher of the matrix and the
prefix path of its runner. macOS also passes `WrapOpenGL_AGL` and gets the Release build type as the default of
`CMakeLists.txt`; Linux passes `CMAKE_BUILD_TYPE=Release`; Windows passes Ninja, `cl`, `CMAKE_BUILD_TYPE=Release` and
`EGTRAIN_NETWORK_TESTS=ON`. The leak check and the nightly sanitizer jobs keep configure lines of their own.

`ci-windows` also holds the vcpkg options: the toolchain of the vcpkg of the runner image (`VCPKG_INSTALLATION_ROOT`),
the `x64-windows` triplet, the manifest `tools/vcpkg/vcpkg.json` and no DLL copy after each link
(`VCPKG_APPLOCAL_DEPS=OFF`; the job puts the DLL folders on `PATH`). The configure step installs the packages of the
manifest at its `builtin-baseline`, into the `installed` folder of that vcpkg (`VCPKG_INSTALLED_DIR`), where the DLL steps
of the job read them. The Windows package job of `package.yml` passes the same vcpkg options on its configure line. The
runner image clones vcpkg in full and a new image updates the clone, so the checkout holds the baseline commit. A
baseline newer than the checkout of the image fails the configure step, because vcpkg reads the port versions from that
checkout.

### Dependency caches

Every leg caches the Qt install (through `jurplel/install-qt-action`, with the
same cache entries as the package jobs), and the Windows leg also caches the vcpkg
binary archives in `runner.temp/vcpkg-binary-cache`, which its configure step names in
`VCPKG_BINARY_SOURCES`. vcpkg names an archive by a hash
that covers its own checkout, its CMake and the compiler, and the runner image fixes all three.
The key of the vcpkg cache is therefore
`vcpkg-x64-windows-<packages>-<baseline>-<image>-v1`: the packages and the `builtin-baseline` of
`tools/vcpkg/vcpkg.json`, which the step `Read the vcpkg manifest` reads, and `ImageOS` and `ImageVersion` of the
runner. A new image or a change to the packages or the baseline writes a new entry. The save step follows the
configure step, which installs the packages, and runs when the restore found no entry with the exact key. To start
over, raise the `-v1` suffix by hand in the restore and save steps. An existing key is never overwritten.
Homebrew and apt packages are not cached.

All three legs also keep a compiler cache, set up by `hendrikmuhs/ccache-action`
before the configure step: ccache on macOS and Linux, sccache on Windows. The
`cache` value of the matrix names the tool, and each configure step passes it as
`CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER`. The Visual Studio
generator ignores a compiler launcher, so the Windows leg configures with Ninja
in the MSVC environment of `ilammy/msvc-dev-cmd` and names `cl`. Its executable
is `build/QEGTRAIN.exe`, not `build/Release/QEGTRAIN.exe`. The Release
configuration adds no `/Zi`, so sccache can store its objects. The key of the
cache is the leg and the runner image, and the action adds a timestamp, so every
run saves a new entry. A run restores the newest entry of its leg and image, or
else the newest of its leg. The post step of the action (`Post Set up compiler
cache`) prints the hits and misses of the job. Each entry holds at most 500 MB,
and GitHub keeps 10 GB for the repository and removes the entries used least
recently first.

The ci presets of the three legs set `EGTRAIN_IPO=OFF`. A Release build links with
interprocedural optimization by default (link-time optimization, or LTCG with
MSVC) when the toolchain supports it; with the option OFF the build skips it.
The optimized link generates the code of the whole program again for every test
executable, which a compiler cache cannot store, so the pull request checks
build without it. The package and release workflows do not pass the option, so
the shipped binaries and the package checks are built with it, and so is any
other build that leaves the option at its default of ON.

A pull request can restore the caches of its own earlier runs and the caches
written on `main`, not the caches of other branches. A push to `main` starts no
run, so only the nightly or manual run on `main` writes the caches that the
first run of a pull request uses. That is enough: the newest compiler cache of
`main` is about a day old, so a pull request compiles again only what the
changes since then affect, and its later runs restore its own cache. The first
run after a key change is cold. GitHub removes a cache that no run has used for
seven days; the nightly run keeps the entries in use.
