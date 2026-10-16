#!/usr/bin/env python3
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def workflow_text(name: str) -> str:
    return (ROOT / ".github/workflows" / name).read_text(encoding="utf-8")


def release_pipeline_text() -> str:
    return workflow_text("release.yml") + "\n" + workflow_text("package.yml")


def trigger_block(text: str) -> str:
    match = re.search(r"^on:\n((?:(?:  .*)?\n)*)", text, re.MULTILINE)
    return match.group(1).rstrip("\n") + "\n" if match else ""


def trigger_events(text: str) -> list[str]:
    return re.findall(r"^  ([a-z_]+):", trigger_block(text), re.MULTILINE)


def job_ids(text: str) -> list[str]:
    jobs = text.split("\njobs:\n", 1)[-1]
    return re.findall(r"^  ([a-z][a-z-]*):\n", jobs, re.MULTILINE)


def job_block(text: str, job: str) -> str:
    jobs = text.split("\njobs:\n", 1)[-1]
    match = re.search(rf"^  {job}:\n((?:(?:    .*)?\n)*)", jobs, re.MULTILINE)
    return match.group(0).rstrip("\n") + "\n" if match else ""


def step_block(job: str, name: str) -> str:
    return next((block for block in job.split("\n      - ") if block.startswith(f"name: {name}\n")), "")


def main() -> None:
    # The release script names the artifact directories and the scene bundles that the workflows must agree with, and the
    # disk image script names the image. Other tests import this file, so the scripts are imported here and not at module level.
    sys.path.insert(0, str(ROOT / "tools/release"))
    import build_release_assets
    import make_dmg

    workflow = (ROOT / ".github/workflows/cmake.yml").read_text(encoding="utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    gui_smoke = (ROOT / "tools/e2e/gui_autostart_smoke.py").read_text(encoding="utf-8")
    release_workflow = release_pipeline_text()
    release_only = workflow_text("release.yml")
    package_only = workflow_text("package.yml")
    package_check = workflow_text("package-check.yml")
    format_workflow = (ROOT / ".github/workflows/format.yml").read_text(encoding="utf-8")
    format_script = (ROOT / "tools/format.py").read_text(encoding="utf-8")
    main_cpp = (ROOT / "EGTRAIN/QEGTRAIN/app/main.cpp").read_text(encoding="utf-8")
    windows_resource = (ROOT / "EGTRAIN/QEGTRAIN/resources/app/egtrain.rc.in").read_text(encoding="utf-8")
    blocks = workflow.split("\n      - ")
    required = {
        "Build": "cmake --build build",
        "Test": "ctest --test-dir build",
    }
    missing = [
        name
        for name, command in required.items()
        if not any(block.startswith(f"name: {name}\n") and command in block for block in blocks)
    ]
    test_steps = [block for block in blocks if block.startswith("name: Test\n")]
    if len(test_steps) != 1 or any(
        "ctest --test-dir build" not in block or "--output-log" not in block
        for block in test_steps
    ):
        missing.append("ctest output logs")
    standalone_smokes = (
        "headless_smoke.py",
        "editor_smoke.sh",
        "roundtrip_smoke.py",
        "bundle_smoke.py",
        "assignment_smoke.py",
        "incident_smoke.py",
        "scene_render_smoke.sh",
        "track_preview_smoke.sh",
        "visual_polish_smoke.sh",
    )
    if any(smoke in workflow for smoke in standalone_smokes):
        missing.append("standalone smoke steps")
    # LeakSanitizer does not run on macOS with Apple silicon, so the leak check is a Linux job. It builds with sanitizers,
    # takes longer than the pull request checks, and runs only in the nightly and manual runs. No other job of the file
    # uses sanitizers.
    leak_job = job_block(workflow, "leak-check")
    leak_upload = step_block(leak_job, "Upload leak check logs")
    suppressions = re.search(r"^      LSAN_OPTIONS: suppressions=\$\{\{ github\.workspace \}\}/(\S+)$", leak_job, re.MULTILINE)
    smoke_script = re.search(r"run: python3 (tools/e2e/\S+\.py) build/QEGTRAIN \"\$RUNNER_TEMP/leak-check\"\n", leak_job)
    reload_tests = re.search(r"-R '\^\(([a-z_|]+)\)\$'", leak_job)
    reload_names = reload_tests.group(1).split("|") if reload_tests else []
    characterization_cmake = (ROOT / "EGTRAIN/QEGTRAIN/tests/characterization/CMakeLists.txt").read_text(encoding="utf-8")
    if (
        "\n  sanitizer:" in workflow
        or workflow.count("EGTRAIN_ENABLE_SANITIZERS=ON") != 1
        or "-DCMAKE_BUILD_TYPE=Debug -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_ENABLE_SANITIZERS=ON" not in leak_job
    ):
        missing.append("sanitizers only in a Debug build of the leak check job")
    if (
        "    runs-on: ubuntu-latest\n" not in leak_job
        or "    if: github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'\n" not in leak_job
    ):
        missing.append("leak check on Linux in the nightly and manual runs only")
    if "      ASAN_OPTIONS: detect_leaks=1\n" not in leak_job:
        missing.append("leak detection in the leak check job")
    if not suppressions or not (ROOT / suppressions.group(1)).is_file():
        missing.append("leak check suppressions file")
    if not smoke_script or not (ROOT / smoke_script.group(1)).is_file():
        missing.append("leak check script for the committed scenes")
    if not {"characterization_repeatable", "characterization_repeat_paimpol"} <= set(reload_names) or any(
        f"add_test(NAME {name}\n" not in characterization_cmake for name in reload_names
    ):
        missing.append("leak check reload runs of registered characterization tests")
    if (
        '--output-log "$RUNNER_TEMP/leak-check/' not in leak_job
        or "actions/upload-artifact@v4" not in leak_upload
        or "if: ${{ !cancelled() }}" not in leak_upload
        or "path: ${{ runner.temp }}/leak-check\n" not in leak_upload
    ):
        missing.append("leak check logs uploaded after a failure")
    if "set_tests_properties(test_gui_autostart_smoke PROPERTIES TIMEOUT 420)" not in cmake:
        missing.append("GUI smoke CTest timeout")
    if "set_tests_properties(test_lebanon_scene_smoke PROPERTIES TIMEOUT 360)" not in cmake:
        missing.append("Lebanon smoke CTest timeout")
    if "set_tests_properties(test_package_contents_smoke PROPERTIES TIMEOUT 420)" not in cmake:
        missing.append("package smoke CTest timeout")
    if "DEFAULT_HORIZON = 200" not in gui_smoke:
        missing.append("bounded GUI smoke horizon")
    if "DEFAULT_MARKER_SECONDS = 300" not in gui_smoke:
        missing.append("hosted-runner GUI startup budget")
    if workflow.count('echo "TMPDIR=$RUNNER_TEMP" >> "$GITHUB_ENV"') != 1:
        missing.append("TMPDIR routing step for CTest logs")
    uploads = [block for block in blocks if block.startswith("name: Upload failure diagnostics\n")]
    if len(uploads) != 1 or any(
        "if: failure()" not in block
        or "actions/upload-artifact@v4" not in block
        or "runner.temp" not in block
        for block in uploads
    ):
        missing.append("failure artifact steps")
    if not uploads or any(
        path not in uploads[0]
        for path in ("/ctest.log", "qegtrain-gui-autostart-smoke.log")
    ):
        missing.append("build job failure logs")
    validation_trigger = workflow.split("\njobs:", 1)[0]
    if re.search(r"^  push:", validation_trigger, re.MULTILINE):
        missing.append("no run for a push to main")
    if "  pull_request:\n    branches: [main]\n" not in validation_trigger:
        missing.append("main pull request validation trigger")
    nightly = re.search(r"^  schedule:\n    - cron: '(\d+) (\d+) \* \* \*'\n(?!    -)", validation_trigger, re.MULTILINE)
    if not nightly or nightly.group(1) == "0":
        missing.append("one nightly run at a minute that is not on the hour")
    if not re.search(r"^  workflow_dispatch:\n(?!    )", validation_trigger, re.MULTILINE):
        missing.append("manual start without inputs")
    concurrency = workflow.split("\nconcurrency:\n", 1)[-1].split("\njobs:", 1)[0]
    if (
        "github.event_name == 'pull_request' && github.ref || github.run_id" not in concurrency
        or "cancel-in-progress: ${{ github.event_name == 'pull_request' }}" not in concurrency
    ):
        missing.append("only a newer push to a pull request cancels a run")
    if "paths:" in validation_trigger:
        missing.append("main validation must not use a code allowlist")
    release_trigger = release_only.split("\njobs:", 1)[0]
    if not re.search(r"push:\n\s+branches:\n\s+- production\n", release_trigger):
        missing.append("production release trigger")
    if not re.search(r"pull_request:\n\s+branches:\n\s+- production\n", release_trigger):
        missing.append("production pull request validation trigger")
    if "paths:" in release_trigger:
        missing.append("production release must not use a code allowlist")
    expected_ignored_paths = {"**.md", "docs/**", "LICENSE", ".github/ISSUE_TEMPLATE/**"}
    for name, trigger, events in (
        ("main", validation_trigger, ("pull_request",)),
        ("production", release_trigger, ("push", "pull_request")),
    ):
        for event in events:
            event_match = re.search(
                rf"^  {event}:\n((?:    .*\n|\n)*)", trigger, re.MULTILINE
            )
            event_block = event_match.group(1) if event_match else ""
            filters = re.search(
                r"^    paths-ignore:\n((?:      - .*\n)+)", event_block, re.MULTILINE
            )
            ignored_paths = set(re.findall(r"      - '([^']+)'", filters.group(1))) if filters else set()
            if ignored_paths != expected_ignored_paths:
                missing.append(f"{name} {event} documentation-only path filters")
    if "      - main\n" in release_trigger or "ci/release-pipeline" in release_trigger:
        missing.append("stale non-production release trigger")
    if "      - 'v*'" not in release_trigger:
        missing.append("version tag release trigger")
    if "  workflow_dispatch:\n" not in release_trigger:
        missing.append("manual release trigger")
    if not re.search(r'set\(EGTRAIN_VERSION "\d+\.\d+\.\d+" CACHE STRING', cmake):
        missing.append("three-component CMake application version")
    if any(
        marker not in content
        for marker, content in (
            ('add_compile_definitions(EGTRAIN_APP_VERSION=\\"${PROJECT_VERSION}\\")', cmake),
            ('file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/EGTRAIN_VERSION"', cmake),
            ("setApplicationVersion(QStringLiteral(EGTRAIN_APP_VERSION))", main_cpp),
            ('VALUE "ProductVersion", "@PROJECT_VERSION@\\0"', windows_resource),
            ('VERSION="$(tr -d \'\\r\\n\' < build/EGTRAIN_VERSION)"', package_only),
            ('project(EGTRAIN VERSION ${EGTRAIN_VERSION} LANGUAGES', cmake),
            ("          VERSION: ${{ needs.version.outputs.version }}\n", release_only),
        )
    ):
        missing.append("single-source application version propagation")
    if "\n  validation:\n" not in release_workflow or release_workflow.count(
        "ctest --test-dir build --output-on-failure"
    ) != 2:
        missing.append("production validation and sanitizer CTest jobs")
    if any(smoke not in release_workflow for smoke in standalone_smokes):
        missing.append("production standalone smoke steps")
    if "\n  sanitizer:\n" not in release_workflow or "EGTRAIN_ENABLE_SANITIZERS=ON" not in release_workflow:
        missing.append("production sanitizer job")
    if any(
        option not in release_workflow
        for option in (
            "UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1",
            "ASAN_OPTIONS: abort_on_error=1:halt_on_error=1",
            'QEGTRAIN_GUI_SMOKE_MARKER_SECONDS: "360"',
            'QEGTRAIN_GUI_SMOKE_SECONDS: "30"',
            "$RUNNER_TEMP/ctest-sanitizer.log",
        )
    ):
        missing.append("production sanitizer diagnostics and time budgets")
    publish_job = release_only.split("\n  release:\n", 1)[1]
    publish_condition = publish_job.split("\n    runs-on:", 1)[0]
    if "needs: [version, validation, sanitizer, package]" not in publish_condition:
        missing.append("release publication validation gates")
    if "if: github.event_name == 'push' && (github.ref == 'refs/heads/production' || startsWith(github.ref, 'refs/tags/v'))" not in publish_condition:
        missing.append("publication restricted to production and tag pushes")
    if not re.search(
        r"if\(EGTRAIN_BUILD_TESTS OR EGTRAIN_PACKAGED_BUILD\).*?"
        r"add_test\(NAME test_updatepreparation COMMAND test_updatepreparation\)",
        cmake, re.DOTALL,
    ) or release_workflow.count("'^test_update(helper|preparation)$'") != 3:
        missing.append("real update preparation tests in all platform packages")
    if 'tag="main-' in release_workflow or 'name="EGTRAIN main build' in release_workflow:
        missing.append("stale main release metadata")
    if 'tag="v${{ needs.version.outputs.version }}"' not in release_workflow:
        missing.append("stable production release tag")
    if (
        release_only.count('"-DEGTRAIN_VERSION=${{ needs.version.outputs.version }}"') != 2
        or package_only.count('"-DEGTRAIN_VERSION=${{ inputs.version }}"') != 3
        or "needs.version" in package_only
    ):
        missing.append("quoted shared release version in all five build jobs")
    # The package workflow selects no version. Its only needs: are the signing job, the finalize job and the installer signing
    # job, which follow the Windows package job.
    if release_only.count("    needs: version\n") != 3 or package_only.count("needs:") != 3:
        missing.append("version selection before all builds")
    for marker in (
        "fetch-depth: 0",
        "run: python3 tools/release/version.py",
        "cancel-in-progress: false",
        "github.event_name == 'push' && 'egtrain-publish'",
        'production_commit="$(git ls-remote origin refs/heads/production | cut -f1)"',
        'if [[ "$production_commit" != "$GITHUB_SHA" ]]',
        'git ls-remote origin "refs/tags/$tag"',
        "draft: true",
        'gh release edit "$RELEASE_TAG" --draft=false --latest="$MAKE_LATEST"',
    ):
        if marker not in release_workflow:
            missing.append("safe versioned publication: " + marker)
    production_metadata = publish_job.split('tag="v${{ needs.version.outputs.version }}"', 1)[-1]
    production_metadata = production_metadata.split("          fi", 1)[0]
    if "prerelease=false" not in production_metadata or "make_latest=true" not in production_metadata:
        missing.append("production releases offered by the stable updater")
    macos_package_verification = step_block(job_block(package_only, "package-macos"), "Verify the packaged app")
    required_macos_validation = (
        "set +e",
        'validation_output="$("$APP/Contents/MacOS/scene_tool" validate "$APP/Contents/Resources/Scenes/Lebanon" 2>&1)"',
        "validation_status=$?",
        "set -e",
        '[ "$validation_status" -eq 0 ]',
    )
    if (
        any(check not in macos_package_verification for check in required_macos_validation)
        or '"$APP/Contents/MacOS/scene_tool" validate "$APP/Contents/Resources/Scenes/Lebanon" 2>&1 | grep' in macos_package_verification
    ):
        missing.append("macOS scene validation status handling")
    required_macos_package_checks = (
        'APP="$PWD/build/QEGTRAIN.app"',
        'test -x "$APP/Contents/MacOS/scene_tool"',
        'test -f "$APP/Contents/Resources/lebanon-case-study.md"',
        'test -d "$APP/Contents/Resources/Scenes/Paimpol"',
        'RUN_DIR="$RUNNER_TEMP/qegtrain-paimpol"',
        'rm -rf "$RUN_DIR"',
        'mkdir -p "$RUN_DIR"',
        'cd "$RUN_DIR"',
        'QEGTRAIN_OUTPUT_DIR="$RUN_DIR" \\',
        '"$APP/Contents/MacOS/QEGTRAIN" --scene "$APP/Contents/Resources/Scenes/Paimpol" -h 300 -g 0 -pax 0 -TSM 0 -RC 0 >"$RUN_DIR/qegtrain.log" 2>&1',
        'run_status=$?',
        'if [ "$run_status" -ne 0 ]; then',
        'cat "$RUN_DIR/qegtrain.log"',
        'grep -q "End of Simulation" "$RUN_DIR/qegtrain.log"',
        'test -f "$RUN_DIR/Output/Paimpol/EnergyConsumptionPerTrain.txt"',
    )
    if any(check not in macos_package_verification for check in required_macos_package_checks):
        missing.append("macOS packaged Paimpol headless smoke")
    app_path_index = macos_package_verification.find('APP="$PWD/build/QEGTRAIN.app"')
    run_dir_index = macos_package_verification.find('cd "$RUN_DIR"')
    if (
        "QT_QPA_PLATFORM=offscreen" in macos_package_verification
        or (app_path_index >= 0 and run_dir_index >= 0 and app_path_index > run_dir_index)
    ):
        missing.append("macOS packaged app path resolution")
    scene_names = (
        "Netherlands",
        "Paimpol",
        "Copenhagen",
        "Milano_Brescia",
        "Assignment_Gvc_Gdg_Ut",
        "Lebanon",
        "Amsterdam_Hilversum_Student",
    )
    if any(f"            {name}\n" not in release_workflow for name in scene_names) or any(
        command not in release_workflow
        for command in (
            'build/scene_tool pack "EGTRAIN/QEGTRAIN/Scenes/$name" "$bundle"',
            'build/scene_tool validate "$bundle"',
            "name: EGTRAIN-scenes",
        )
    ):
        missing.append("seven deterministic release scene bundles")
    # The macOS package job packs the bundles that the release script reads, and the release job publishes every file
    # that the script wrote, failing when the glob matches none.
    scene_bundle_step = step_block(job_block(package_only, "package-macos"), "Generate scene bundles")
    packed_scenes = re.search(r"\n          scenes=\(\n((?:            \S+\n)+)          \)\n", scene_bundle_step)
    if not packed_scenes or set(packed_scenes.group(1).split()) != set(build_release_assets.SCENES):
        missing.append("scene bundles packed by the package workflow are the scenes that the release script reads")
    release_job = job_block(release_only, "release")
    prepare_step = step_block(release_job, "Prepare release")
    if (
        "          files: release-assets/*\n" not in prepare_step
        or "          fail_on_unmatched_files: true\n" not in prepare_step
    ):
        missing.append("release published from the files of the output directory, failing when none matches")
    if "python3 tools/e2e/headless_smoke.py 1 2 3 4 5 6 7" not in release_workflow:
        missing.append("production headless smoke of all seven canonical scenes")
    if '"$APP/Contents/Frameworks/QtNetwork.framework"' not in release_workflow:
        missing.append("macOS Qt Network package verification")
    if '"Qt5Network.dll"' not in release_workflow:
        missing.append("Windows Qt Network package verification")
    if "libqt5network5" not in release_workflow or "libQt5Network.so*" not in release_workflow:
        missing.append("Linux Qt Network package verification")
    if release_workflow.count("-DEGTRAIN_PACKAGED_BUILD=ON") != 3:
        missing.append("packaged-build updater gate on all platform packages")
    if release_workflow.count("Verify update helper transaction") != 3 or any(
        helper not in release_workflow
        for helper in (
            "QEGTRAIN.app/Contents/MacOS/egtrain_update_helper",
            "egtrain_update_helper.exe",
            "squashfs-root/usr/bin/egtrain_update_helper",
        )
    ):
        missing.append("packaged update helper verification")
    # The release job downloads the artifacts, builds the release assets with the script and publishes them. The manifest,
    # its hashes and the Windows archive come from the script only.
    if re.findall(r"^      - name: (.+)$", release_job, re.MULTILINE) != [
        "Download all artifacts",
        "List downloaded files",
        "Set release metadata",
        "Build the release assets",
        "Prepare release",
        "Publish complete release",
    ]:
        missing.append("release job steps: download, list, metadata, build the release assets, prepare, publish")
    for word in ("sha256", "shasum", "Get-FileHash", "unzip", "Compress-Archive", "stat -c", "update-manifest"):
        if any(word.lower() in text.lower() for text in (release_only, package_only, package_check)):
            missing.append(f"no {word} in the workflows, as the manifest, its hashes and the archive come from the script only")
    if (
        any(f"os: {runner}\n" not in workflow for runner in ("macos-latest", "windows-latest", "ubuntu-latest"))
        or "      fail-fast: false\n" not in workflow
    ):
        missing.append("independent macOS, Windows and Linux validation legs")
    windows_toolchain = (
        "version: '5.15.2'",
        "arch: win64_msvc2019_64",
        "modules: qtcharts",
        "ilammy/msvc-dev-cmd@v1",
        "vcpkg install zeromq cppzmq nlohmann-json --triplet x64-windows",
        "-DVCPKG_TARGET_TRIPLET=x64-windows",
        "scripts/buildsystems/vcpkg.cmake",
    )
    if any(entry not in workflow or entry not in release_workflow for entry in windows_toolchain):
        missing.append("Windows toolchain shared with the release workflow")
    # vcpkg would copy its DLLs next to every executable after linking, a step that fails at
    # random when the new file is still in use. The job puts the DLL directories on PATH instead.
    if "-DVCPKG_APPLOCAL_DEPS=OFF" not in workflow or "installed/x64-windows/bin\" | Out-File -Append" not in workflow:
        missing.append("Windows build without the per-executable DLL copy, with the DLL directories on PATH")
    # Each platform configures with the option, so a warning in a strict target fails its leg.
    if workflow.count("cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_WARNINGS_AS_ERRORS=ON") != 3:
        missing.append("warnings as errors in the macOS, Windows and Linux configure steps")
    apt_installs = re.findall(r"apt-get install -y((?:.*\\\n)*.*\n)", workflow)
    apt_packages = [package for install in apt_installs for package in install.replace("\\", " ").split()]
    if len(apt_installs) != 2 or apt_installs[0] != apt_installs[1]:
        missing.append("the same Linux packages in the build and leak check jobs")
    if not apt_packages or any(
        not re.search(rf"(?<![\w.+-]){re.escape(package)}(?![\w.+-])", release_workflow)
        for package in apt_packages
    ):
        missing.append("Linux packages shared with the release workflow")
    cache_keys = re.findall(r"^\s+key: (\S+)\s*$", workflow, re.MULTILINE)
    if not cache_keys or any(not re.search(r"-v\d+$", key) for key in cache_keys):
        missing.append("fixed dependency cache keys with a manual version suffix")
    test_step = test_steps[0] if test_steps else ""
    build_step = next((block for block in blocks if block.startswith("name: Build\n")), "")
    if "ctest --test-dir build -C Release" not in test_step:
        missing.append("multi-config CTest configuration")
    if "--parallel ${{ matrix.jobs }}" not in test_step or "--output-on-failure" not in test_step:
        missing.append("parallel CTest run with the jobs of the matrix")
    if "cmake --build build --config Release" not in build_step:
        missing.append("multi-config build configuration")
    if "COMMAND python3" in cmake or "Python3_EXECUTABLE" not in cmake:
        missing.append("CTest uses the configured Python interpreter")
    for bash_command in re.finditer(r"COMMAND bash", cmake):
        guards = re.findall(
            r"^\s*(if\(.*\)|else\(\)|elseif\(.*\)|endif\(\))\s*$",
            cmake[: bash_command.start()],
            re.MULTILINE,
        )
        if not guards or guards[-1] not in ("if(UNIX)", "if(APPLE)"):
            missing.append("bash CTest commands only on UNIX")
            break
    if not re.search(
        r"if\(WIN32\)(?:(?!endif\(\)).)*?pe_image_size\.py(?:(?!endif\(\)).)*?--max-bytes",
        cmake,
        re.DOTALL,
    ):
        missing.append("Windows image size CTest")
    package_step = next((block for block in blocks if block.startswith("name: Start an assembled Windows package\n")), "")
    package_assembly = (
        'installed/x64-windows/bin/*.dll" $dist/',
        'windeployqt.exe" --release --no-translations --compiler-runtime --no-opengl-sw --no-angle'
        ' --no-system-d3d-compiler --no-virtualkeyboard --no-quick-import "$dist/QEGTRAIN.exe"',
        'Copy-Item -Recurse EGTRAIN/QEGTRAIN/Scenes "$dist/Scenes"',
    )
    if (
        "tools/release/package_start_smoke.py $dist" not in package_step
        or any(entry not in package_step or entry not in release_workflow for entry in package_assembly)
    ):
        missing.append("Windows package start check with the assembly commands of the release workflow")
    # Qt 5.15 has no TLS library on Windows, so the update requests go through WinHTTP there. The Windows leg sends one real request
    # to the GitHub release list through that transport and fails when no HTTPS response arrives. The test is registered only when
    # the configuration asks for it, so no other test run needs internet access. The program returns one code for a rate limit or
    # an outage of GitHub, which CTest shows as skipped.
    https_probe = (ROOT / "EGTRAIN/QEGTRAIN/tests/test_updatetransport_https.cpp").read_text(encoding="utf-8")
    https_skip = re.search(r"^constexpr int kExitSkipped = (\d+);$", https_probe, re.MULTILINE)
    https_registered = re.search(
        r"\n    if\(EGTRAIN_NETWORK_TESTS\)\n        add_test\(NAME test_updatetransport_https COMMAND test_updatetransport_https\)\n"
        r"        set_tests_properties\(test_updatetransport_https PROPERTIES TIMEOUT \d+ SKIP_RETURN_CODE (\d+)\)\n    endif\(\)\n",
        cmake,
    )
    packaged_tests = re.search(r"^if\(EGTRAIN_BUILD_TESTS OR EGTRAIN_PACKAGED_BUILD\)\n(.*?)^endif\(\)", cmake, re.MULTILINE | re.DOTALL)
    if not packaged_tests or "add_executable(test_updatetransport_https " not in packaged_tests.group(1):
        missing.append("HTTPS probe program built in a packaged build")
    if (
        'option(EGTRAIN_NETWORK_TESTS "Register the tests that need internet access" OFF)\n' not in cmake
        or not https_registered
        or not https_skip
        or https_registered.group(1) != https_skip.group(1)
        or "egtrain_label_tests(network test_updatetransport_https)\n" not in cmake
        or not re.search(r"egtrain_label_tests\(integration\b[^)]*\btest_updatetransport_https\b", cmake)
    ):
        missing.append("HTTPS probe test registered only with EGTRAIN_NETWORK_TESTS, labelled network, skipped on the code that the program returns")
    if not re.search(r"add_test\(NAME test_updatetransport_https_cases COMMAND \$\{Python3_EXECUTABLE\}\n[^\n]*test_updatetransport_https_cases\.py \$<TARGET_FILE:test_updatetransport_https>\)", cmake):
        missing.append("HTTPS probe answers test registered for every platform")
    build_job = job_block(workflow, "build")
    build_step_names = re.findall(r"^      - name: (.+)$", build_job, re.MULTILINE)
    https_step = step_block(build_job, "Check HTTPS (Windows)")
    if (
        workflow.count("-DEGTRAIN_NETWORK_TESTS=ON") != 1
        or "-DEGTRAIN_NETWORK_TESTS=ON" not in step_block(build_job, "Configure (Windows)")
        or "-LE network" not in test_step
        or "if: runner.os == 'Windows'\n" not in https_step
        or "        run: ctest --test-dir build -C Release -L network --no-tests=error --output-on-failure\n" not in https_step
        or "Check HTTPS (Windows)" not in build_step_names
        or build_step_names.index("Test") > build_step_names.index("Check HTTPS (Windows)")
    ):
        missing.append("Windows HTTPS check after the tests, with the network tests built for the Windows leg only and left out of the Test step")
    if not re.search(
        r"if\(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4\)\s*message\(FATAL_ERROR",
        cmake,
    ):
        missing.append("64-bit Windows configure check")
    # The format check installs the clang-format version that tools/format.py accepts.
    format_version = re.search(r'^CLANG_FORMAT_VERSION = "([0-9.]+)"$', format_script, re.MULTILINE)
    if (
        not format_version
        or f"pip install 'clang-format=={format_version.group(1)}'" not in format_workflow
        or "run: python3 tools/format.py\n" not in format_workflow
        or "  pull_request:\n" not in format_workflow
    ):
        missing.append("format check on pull requests with the clang-format version of tools/format.py")
    # The package jobs live in a reusable workflow that the release workflow and the package check call. The two signing jobs
    # of that workflow ask for two read permissions, and a called workflow cannot hold more than its caller grants, so both
    # callers grant exactly those two. Only the release workflow passes the signing token, by name.
    package_call = (
        "    needs: version\n"
        "    uses: ./.github/workflows/package.yml\n"
        "    permissions:\n"
        "      actions: read\n"
        "      contents: read\n"
        "    with:\n"
        "      version: ${{ needs.version.outputs.version }}\n"
    )
    signing_token_call = "    secrets:\n      SIGNPATH_API_TOKEN: ${{ secrets.SIGNPATH_API_TOKEN }}\n"
    if (
        trigger_events(release_only) != ["push", "pull_request", "workflow_dispatch"]
        or "\nconcurrency:\n"
        "  group: ${{ github.event_name == 'push' && 'egtrain-publish' || github.run_id }}\n"
        "  cancel-in-progress: false\n\njobs:\n" not in release_only
        or release_only.count("permissions:") != 2
    ):
        missing.append("release triggers, concurrency and permissions")
    # The only write permission of the release workflow is the one of the job that publishes.
    if (
        re.findall(r"^ *([\w-]+): write$", release_only, re.MULTILINE) != ["contents"]
        or "    runs-on: ubuntu-latest\n    permissions:\n      contents: write\n" not in job_block(release_only, "release")
    ):
        missing.append("release workflow whose only write permission is contents: write in the release job")
    if job_ids(release_only) != ["version", "validation", "sanitizer", "package", "release"]:
        missing.append("release jobs: version, validation, sanitizer, package, release")
    if job_block(release_only, "package") != "  package:\n" + package_call + signing_token_call:
        missing.append(
            "release package job calls the package workflow with the selected version, the two read permissions "
            "that the signing job asks for and the signing token by name"
        )
    if re.findall(r"\bsecrets\.\w+", release_only) != ["secrets.SIGNPATH_API_TOKEN"] or "inherit" in release_only:
        missing.append("release workflow whose only credential is the signing token that it passes by name to the package workflow")
    if trigger_block(package_only) != (
        "  workflow_call:\n    inputs:\n      version:\n        required: true\n        type: string\n"
        "    secrets:\n      SIGNPATH_API_TOKEN:\n        required: false\n"
    ):
        missing.append("package workflow starts only by call, with the version as its only input and an optional signing token")
    if (
        "\npermissions:\n  contents: read\n\njobs:\n" not in package_only
        or package_only.count("permissions:") != 3
        or re.search(r":\s*write", package_only)
    ):
        missing.append("package workflow with read permission at the top, one more block in each signing job, and no write permission")
    # The signing token is named in one step of each Windows signing job, the certificate and its password of the macOS
    # signing in the one step that imports them, and the key and the password of the notarization in the two steps that
    # run the notarization script. No other step of any job can read them.
    if re.findall(r"\bsecrets\.\w+", package_only) != [
        "secrets.MACOS_CERTIFICATE",
        "secrets.MACOS_CERTIFICATE_PASSWORD",
        "secrets.MACOS_NOTARY_KEY",
        "secrets.MACOS_NOTARY_PASSWORD",
        "secrets.MACOS_NOTARY_KEY",
        "secrets.MACOS_NOTARY_PASSWORD",
        "secrets.SIGNPATH_API_TOKEN",
        "secrets.SIGNPATH_API_TOKEN",
    ]:
        missing.append(
            "package workflow whose only credentials are the macOS certificate and notarization key with their passwords and the Windows signing token"
        )
    package_jobs = ["package-macos", "package-windows", "sign-windows", "finalize-windows", "sign-windows-installer", "package-linux"]
    if job_ids(package_only) != package_jobs:
        missing.append("package workflow jobs: " + ", ".join(package_jobs))
    # The release script reads one artifact directory for each package, the Windows payload and the scene bundles. The
    # package workflow uploads exactly those names, and each package as the file that the script publishes. A name with
    # the release version in it is uploaded with the version input of the workflow.
    for _key, template, directory in build_release_assets.PACKAGES:
        file = template.format(version="${{ inputs.version }}")
        if not re.search(
            rf"^          name: {re.escape(directory)}\n          path: (?:\S*/)?{re.escape(file)}\n", package_only, re.MULTILINE
        ):
            missing.append(f"package artifact {directory} uploaded as the file {file} that the release script reads")
    for directory in (build_release_assets.WINDOWS_PAYLOAD, build_release_assets.SCENES_ARTIFACT):
        if f"          name: {directory}\n          path: " not in package_only:
            missing.append(f"package artifact {directory} uploaded under the name that the release script reads")
    # The application draws with the raster engine, so the Windows package job leaves out the software OpenGL, ANGLE,
    # Direct3D compiler, Qt Quick, QML and virtual keyboard files, fails when one is present and starts a copy of the package.
    windows_job = job_block(package_only, "package-windows")
    verify_step = step_block(windows_job, "Verify the package is complete")
    forbidden_files = (
        '          foreach ($name in @("opengl32sw.dll","d3dcompiler_47.dll","libEGL.dll","libGLESv2.dll","Qt5Quick*","Qt5Qml*","*VirtualKeyboard*")) {\n'
        '            $found = @(Get-ChildItem "$dist" -Recurse -File -Filter $name)\n'
        '            if ($found.Count -gt 0) { throw "unexpected $name in the package: $($found[0].FullName)" }\n'
        "          }\n"
    )
    if forbidden_files not in verify_step or verify_step.find(forbidden_files) > verify_step.find("Write-Host"):
        missing.append("Windows package check that forbids the OpenGL, ANGLE, Direct3D compiler, Qt Quick, QML and virtual keyboard files")
    for required_file in ("iconengines/qsvgicon.dll", "styles/qwindowsvistastyle.dll"):
        if f'          if (-not (Test-Path "$dist/{required_file}")) {{ throw "missing {required_file}" }}\n' not in verify_step:
            missing.append(f"Windows package check that requires {required_file}")
    deploy_options = ("--no-opengl-sw", "--no-angle", "--no-system-d3d-compiler", "--no-virtualkeyboard", "--no-quick-import")
    package_deploy = [line.strip() for line in windows_job.splitlines() if "windeployqt.exe" in line]
    start_deploy = [line.strip() for line in package_step.splitlines() if "windeployqt.exe" in line]
    if len(package_deploy) != 1 or any(option not in package_deploy[0] for option in deploy_options):
        missing.append("windeployqt options of the Windows package job that leave out the OpenGL, ANGLE, Direct3D compiler, Qt Quick and virtual keyboard files")
    if package_deploy != start_deploy:
        missing.append("the same windeployqt line in the Windows package job and in the Windows package start check")
    windows_steps = ("Assemble package", "Verify the package is complete", "Report package size", "Start the package", "Upload artifact")
    windows_step_names = re.findall(r"^      - name: (.+)$", windows_job, re.MULTILINE)
    step_positions = [windows_step_names.index(name) if name in windows_step_names else -1 for name in windows_steps]
    if -1 in step_positions or step_positions != sorted(step_positions):
        missing.append("Windows package steps in the order " + ", ".join(windows_steps))
    size_step = step_block(windows_job, "Report package size")
    if (
        "Get-ChildItem dist/QEGTRAIN -Recurse -File" not in size_step
        or '"Windows package: $($files.Count) files, $bytes bytes" | Tee-Object -Append -FilePath $env:GITHUB_STEP_SUMMARY' not in size_step
    ):
        missing.append("Windows package size in the job summary")
    start_step = step_block(windows_job, "Start the package")
    python_setup = next((block for block in windows_job.split("\n      - ") if block.startswith("uses: actions/setup-python@v5\n")), "")
    if (
        not start_step.strip().endswith('python tools/release/package_start_smoke.py "$start"')
        or '$start = "$env:RUNNER_TEMP/package-start/QEGTRAIN"' not in start_step
        or "Copy-Item -Recurse dist/QEGTRAIN/* $start" not in start_step
        or start_step.count("dist/QEGTRAIN") != 1
        or 'PYTHONUTF8: "1"' not in start_step
        or "python-version: '3.12'" not in python_setup
        or not 0 <= windows_job.find("uses: actions/setup-python@v5") < windows_job.find("name: Start the package")
    ):
        missing.append("Windows package start of a copy under RUNNER_TEMP with Python set up")
    # The package job starts the HTTPS probe from that copy, with only the Windows directories on PATH, so it loads the Qt files
    # of the package. The exit code for a rate limit or an outage of GitHub does not fail the job.
    https_package = step_block(windows_job, "Check HTTPS from the package")
    https_order = ("Start the package", "Check HTTPS from the package", "Upload artifact")
    https_positions = [windows_step_names.index(name) if name in windows_step_names else -1 for name in https_order]
    if (
        -1 in https_positions
        or https_positions != sorted(https_positions)
        or '$start = "$env:RUNNER_TEMP/package-start/QEGTRAIN"\n' not in https_package
        or "Copy-Item $probe.FullName $start\n" not in https_package
        or '$env:PATH = "$env:SystemRoot\\System32;$env:SystemRoot"\n' not in https_package
        or '& "$start/test_updatetransport_https.exe"' not in https_package
        or not https_skip
        or f"$LASTEXITCODE -ne {https_skip.group(1)}" not in https_package
    ):
        missing.append("Windows package check of HTTPS from a copy of the package with only the Windows directories on PATH")
    # The job uploads the directory that it verified, sized and started, with its hidden files. It writes no archive:
    # the finalize job writes the portable archive from the uploaded files.
    if step_block(windows_job, "Upload artifact") != (
        "name: Upload artifact\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        "          name: QEGTRAIN-windows-x64-payload\n"
        "          path: dist/QEGTRAIN\n"
        "          include-hidden-files: true\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("Windows package upload of the directory that is built, with its hidden files, as the payload artifact")
    if "zip" in windows_job.lower():
        missing.append("Windows package job without an archive")
    # Every run, a pull request included, prints the signature state of the package before it is uploaded. The step has no
    # condition and no mode that fails.
    report_lines = step_block(windows_job, "Report the code signatures").strip().splitlines()
    if (
        not report_lines
        or report_lines[-1].strip() != "run: ./tools/release/verify_signatures.ps1 -Directory dist/QEGTRAIN -Report"
        or "        shell: pwsh" not in report_lines
        or any(line.strip().startswith("if:") for line in report_lines)
        or not 0 <= windows_step_names.index("Report package size") < windows_step_names.index("Report the code signatures")
        or windows_step_names.index("Report the code signatures") > windows_step_names.index("Upload artifact")
    ):
        missing.append("Windows package report of the code signatures of the unsigned package on every run")
    # The signing job runs only for a push to production or a v* tag in a repository that opts in by a variable, so a pull
    # request, a manual run and a repository without the variable skip it. It sends the three program files and no library to
    # the signing service, puts the signed files into the payload, checks them and replaces the payload artifact, which the
    # finalize job reads. Its token is the input of its signing step; only this job, the installer signing job and the macOS
    # package job have an environment.
    sign_job = job_block(package_only, "sign-windows")
    sign_steps = [
        "Download the package",
        "Collect the files to sign",
        "Upload the files to sign",
        "Sign the files",
        "Put the signed files into the package",
        "Verify the signatures",
        "Upload the signed package",
    ]
    if re.findall(r"^      - name: (.+)$", sign_job, re.MULTILINE) != sign_steps:
        missing.append("Windows signing steps: " + ", ".join(sign_steps))
    if (
        re.findall(r"^    needs: (.+)$", sign_job, re.MULTILINE) != ["package-windows"]
        or re.findall(r"^    runs-on: (.+)$", sign_job, re.MULTILINE) != ["windows-latest"]
        or re.findall(r"^    environment: (.+)$", sign_job, re.MULTILINE) != ["windows-signing"]
        or package_only.count("environment:") != 3
    ):
        missing.append("Windows signing job after the Windows package job, on windows-latest, in an environment that only the two signing jobs and the macOS package job use")
    # The signing action reads the run and downloads the uploaded files with the token of the job (its documentation names
    # actions: read and contents: read), and the workflow grants contents: read only. The job holds the second block of the
    # workflow, with nothing else in it.
    if (
        sign_job.count("permissions:") != 1
        or "    environment: windows-signing\n    permissions:\n      actions: read\n      contents: read\n    timeout-minutes: 90\n"
        not in sign_job
    ):
        missing.append("Windows signing job with its own permissions: actions: read and contents: read")
    if re.findall(r"^    if: (.+)$", sign_job, re.MULTILINE) != [
        "${{ github.event_name == 'push' && (github.ref == 'refs/heads/production' || startsWith(github.ref, 'refs/tags/v')) "
        "&& vars.WINDOWS_SIGNING == 'signpath' }}"
    ]:
        missing.append("Windows signing job only for a push to production or a v* tag when the variable WINDOWS_SIGNING is signpath")
    if step_block(sign_job, "Download the package") != step_block(job_block(package_only, "finalize-windows"), "Download the package"):
        missing.append("Windows signing download of the payload artifact into payload, as the finalize job does")
    sign_collect = step_block(sign_job, "Collect the files to sign")
    if (
        'foreach ($name in @("QEGTRAIN.exe", "egtrain_update_helper.exe", "scene_tool.exe")) {\n' not in sign_collect
        or sign_collect.count("Copy-Item") != 1
        or ".dll" in sign_collect.lower()
        or "*" in sign_collect
    ):
        missing.append("Windows signing that collects the three program files and no library")
    if step_block(sign_job, "Upload the files to sign") != (
        "name: Upload the files to sign\n"
        "        id: upload-unsigned\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        "          name: QEGTRAIN-windows-x64-unsigned\n"
        "          path: unsigned\n"
        "          overwrite: true\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("Windows signing upload of the collected files, replaceable on a re-run, as the artifact that the signing request names")
    sign_request = step_block(sign_job, "Sign the files")
    if any(
        part not in sign_request
        for part in (
            "        uses: signpath/github-action-submit-signing-request@v3\n",
            "          api-token: ${{ secrets.SIGNPATH_API_TOKEN }}\n",
            "          github-artifact-id: ${{ steps.upload-unsigned.outputs.artifact-id }}\n",
            "          wait-for-completion: true\n",
            "          output-artifact-directory: signed\n",
        )
    ) or package_only.count("SIGNPATH_API_TOKEN") != 3 or "set -x" in package_only:
        missing.append("Windows signing request for the uploaded files with the token as the input of the signing step only")
    sign_put = step_block(sign_job, "Put the signed files into the package")
    if (
        'foreach ($name in @("QEGTRAIN.exe", "egtrain_update_helper.exe", "scene_tool.exe")) {\n' not in sign_put
        or 'Copy-Item -LiteralPath "signed/$name" -Destination payload -Force\n' not in sign_put
        or sign_put.count("Copy-Item") != 1
    ):
        missing.append("Windows signing that puts the three signed program files into the payload")
    sign_verify = step_block(sign_job, "Verify the signatures")
    if (
        "        run: ./tools/release/verify_signatures.ps1 -Directory payload -Require -ExpectedSubject $env:EXPECTED_SUBJECT\n"
        not in sign_verify
        or "          EXPECTED_SUBJECT: ${{ vars.WINDOWS_SIGNING_SUBJECT }}\n" not in sign_verify
        or "-Report" in sign_verify
        or "-AllowUntrustedRoot" in package_only
        or "-NoTimestampCheck" in package_only
    ):
        missing.append("Windows signing check of the payload that requires the expected signer, a timestamp and a trusted chain")
    if step_block(sign_job, "Upload the signed package") != (
        "name: Upload the signed package\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        "          name: QEGTRAIN-windows-x64-payload\n"
        "          path: payload\n"
        "          include-hidden-files: true\n"
        "          overwrite: true\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("Windows signing upload of the checked payload in place of the payload artifact")
    # The check script reads the signature, the digest algorithm, the timestamp and the signer, and fails only in the mode that
    # requires them. Its test signs copies of scene_tool.exe with a throwaway certificate that it keeps in no file, and CTest
    # runs it on Windows only.
    verify_script = (ROOT / "tools/release/verify_signatures.ps1").read_text(encoding="utf-8")
    verify_test = (ROOT / "tools/release/test_verify_signatures.ps1").read_text(encoding="utf-8")
    if any(
        part not in verify_script
        for part in (
            "Get-AuthenticodeSignature -LiteralPath $Path",
            "$sha256 = '2.16.840.1.101.3.4.2.1'",
            "$accepted = @('Valid')\n",
            "if ($AllowUntrustedRoot) { $accepted += @('UnknownError', 'NotTrusted') }\n",
            "if ($accepted -notcontains $status) { $problems += ",
            "if ($ExpectedSubject -and $subject -ne $ExpectedSubject) { $problems += ",
            "if (-not $NoTimestampCheck -and -not $signature.TimeStamperCertificate) { $problems += ",
            "if ($digest[0] -ne $sha256) { $problems += ",
            "[switch]$Require",
            "[switch]$Report",
            "[switch]$AllowUntrustedRoot",
            "[switch]$NoTimestampCheck",
            "if ($Require -and $failed -gt 0) { exit 1 }\nexit 0\n",
        )
    ) or verify_script.count("exit 1") != 1:
        missing.append("signature check script of the status, the SHA-256 digest, the timestamp and the signer that fails only with -Require")
    if (
        any(part not in verify_test for part in ("New-SelfSignedCertificate -Type CodeSigningCert", "Set-AuthenticodeSignature", "'SHA1'"))
        or any(part in verify_test for part in ("Export-", ".pfx", "Import-Certificate", "Cert:\\CurrentUser\\Root", "Cert:\\LocalMachine"))
        or "\tif(WIN32)\n\t\tfind_program(EGTRAIN_POWERSHELL NAMES pwsh powershell)\n" not in cmake
        or "-File ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_verify_signatures.ps1" not in cmake
        or "-SceneTool $<TARGET_FILE:scene_tool>" not in cmake
        or not re.search(r"egtrain_label_tests\(integration\b[^)]*\btest_verify_signatures\b", cmake)
    ):
        missing.append("signature check test with a throwaway certificate that is never exported, registered for Windows only")
    # The finalize job builds the installer and the portable archive from the payload that the Windows package job uploaded,
    # so both hold the same files. It installs, upgrades and uninstalls the installer, starts the portable archive after
    # it extracts it, and uploads the two files only after both checks passed. The archive comes from the release script.
    finalize_job = job_block(package_only, "finalize-windows")
    finalize_steps = (
        "Download the package",
        "Find Inno Setup",
        "Build the installers",
        "Smoke test the installer",
        "Build the portable archive",
        "Start the portable archive",
        "Upload the installer",
        "Upload the portable archive",
    )
    finalize_step_names = re.findall(r"^      - name: (.+)$", finalize_job, re.MULTILINE)
    finalize_positions = [finalize_step_names.index(name) if name in finalize_step_names else -1 for name in finalize_steps]
    if -1 in finalize_positions or finalize_positions != sorted(finalize_positions):
        missing.append("Windows finalize steps in the order " + ", ".join(finalize_steps))
    if (
        re.findall(r"^    needs: (.+)$", finalize_job, re.MULTILINE) != ["[package-windows, sign-windows]"]
        or re.findall(r"^    runs-on: (.+)$", finalize_job, re.MULTILINE) != ["windows-latest"]
    ):
        missing.append("Windows finalize job after the Windows package job and the signing job, on windows-latest")
    # Signing is skipped, not failed, when it is not set up, and the finalize job then reads the unsigned package. When signing
    # runs and fails the finalize job does not run, so the installer and the archive never come from unsigned files.
    if re.findall(r"^    if: (.+)$", finalize_job, re.MULTILINE) != [
        "${{ !cancelled() && needs.package-windows.result == 'success' && "
        "(needs.sign-windows.result == 'success' || needs.sign-windows.result == 'skipped') }}"
    ]:
        missing.append("Windows finalize job that runs after a skipped or successful signing job and not after a failed one")
    if step_block(finalize_job, "Download the package") != (
        "name: Download the package\n"
        "        uses: actions/download-artifact@v4\n"
        "        with:\n"
        "          name: QEGTRAIN-windows-x64-payload\n"
        "          path: payload\n"
    ):
        missing.append("Windows finalize download of the payload artifact of the Windows package job into payload")
    installer_step = step_block(finalize_job, "Build the installers")
    if (
        '$payload = (Resolve-Path payload).Path\n' not in installer_step
        or '"/DVersion=${{ inputs.version }}" "/DPayload=$payload" "/O$PWD\\installer-out" installer\\egtrain.iss\n' not in installer_step
        or installer_step.count("/DPayload=$payload") != 2
        or "/DVersion=$newer" not in installer_step
    ):
        missing.append("Windows finalize installers compiled from the payload, the second one with a newer version")
    portable_step = step_block(finalize_job, "Build the portable archive")
    if (
        '--version "${{ inputs.version }}" --payload payload --output portable\n' not in portable_step
        or finalize_job.count("--payload payload") != 1
    ):
        missing.append("Windows finalize portable archive written by the release script from the same payload directory")
    smoke_step = step_block(finalize_job, "Smoke test the installer")
    if any(
        part not in smoke_step
        for part in (
            "./tools/release/installer_smoke.ps1",
            '-Installer "installer-out/EGTRAIN-Setup-${{ inputs.version }}-x64.exe"',
            '-UpgradeInstaller "installer-upgrade/EGTRAIN-Setup-$($env:UPGRADE_VERSION)-x64.exe"',
            "-Payload payload",
            '-Version "${{ inputs.version }}"',
            'PYTHONUTF8: "1"',
        )
    ):
        missing.append("Windows finalize installer smoke on the installers and the payload of the job")
    portable_start = step_block(finalize_job, "Start the portable archive")
    if (
        'python -m zipfile -e "portable/EGTRAIN-Portable-${{ inputs.version }}-x64.zip" $start' not in portable_start
        or not portable_start.strip().endswith('python tools/release/package_start_smoke.py "$start"')
        or 'PYTHONUTF8: "1"' not in portable_start
    ):
        missing.append("Windows finalize start of the extracted portable archive")
    if "uses: actions/setup-python@v5" not in finalize_job or "python-version: '3.12'" not in finalize_job:
        missing.append("Windows finalize job with Python set up")
    if step_block(finalize_job, "Upload the installer") != (
        "name: Upload the installer\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        "          name: EGTRAIN-windows-x64-installer\n"
        "          path: installer-out/EGTRAIN-Setup-${{ inputs.version }}-x64.exe\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("Windows finalize upload of the installer that the smoke test installed")
    if step_block(finalize_job, "Upload the portable archive") != (
        "name: Upload the portable archive\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        "          name: EGTRAIN-windows-x64-portable\n"
        "          path: portable/EGTRAIN-Portable-${{ inputs.version }}-x64.zip\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("Windows finalize upload of the portable archive that was started")
    # The installer is signed after it is compiled, in a job of its own that runs only when sign-windows signed the program
    # files. The finalize job compiles the installer and writes the archive from the payload that sign-windows replaced, and
    # it signs and changes nothing, so both come from the signed files. The installer job sends the installer alone to the
    # signing service, requires valid signatures by the expected signer on the installer and on the three program files of the
    # payload, and replaces the installer artifact only after both checks passed.
    if any(
        word in finalize_job.lower()
        for word in ("signtool", "authenticode", "signpath", "environment:", "secrets.", "copy-item", "move-item", "remove-item")
    ):
        missing.append("Windows finalize job that signs nothing and changes no file of the payload")
    installer_artifact, installer_template = next(
        (directory, template) for key, template, directory in build_release_assets.PACKAGES if key == "windows-x64-installer"
    )
    installer_file = installer_template.format(version="${{ inputs.version }}")
    installer_job = job_block(package_only, "sign-windows-installer")
    installer_sign_steps = [
        "Download the package",
        "Download the installer",
        "Upload the installer to sign",
        "Sign the installer",
        "Verify the program files",
        "Verify the installer",
        "Upload the signed installer",
    ]
    if re.findall(r"^      - name: (.+)$", installer_job, re.MULTILINE) != installer_sign_steps:
        missing.append("Windows installer signing steps: " + ", ".join(installer_sign_steps))
    if (
        re.findall(r"^    needs: (.+)$", installer_job, re.MULTILINE) != ["[sign-windows, finalize-windows]"]
        or re.findall(r"^    if: (.+)$", installer_job, re.MULTILINE) != ["${{ needs.sign-windows.result == 'success' }}"]
        or re.findall(r"^    runs-on: (.+)$", installer_job, re.MULTILINE) != ["windows-latest"]
        or re.findall(r"^    environment: (.+)$", installer_job, re.MULTILINE) != ["windows-signing"]
        or installer_job.count("permissions:") != 1
        or "    environment: windows-signing\n    permissions:\n      actions: read\n      contents: read\n    timeout-minutes: 90\n"
        not in installer_job
    ):
        missing.append(
            "Windows installer signing job after the signing job and the finalize job, only when the program files were signed, "
            "in the environment of the signing job with the same two read permissions"
        )
    if step_block(installer_job, "Download the package") != step_block(finalize_job, "Download the package"):
        missing.append("Windows installer signing download of the payload artifact that the installer and the archive were built from")
    if step_block(installer_job, "Download the installer") != (
        "name: Download the installer\n"
        "        uses: actions/download-artifact@v4\n"
        "        with:\n"
        f"          name: {installer_artifact}\n"
        "          path: installer\n"
    ):
        missing.append("Windows installer signing download of the installer that the finalize job uploaded")
    if step_block(installer_job, "Upload the installer to sign") != (
        "name: Upload the installer to sign\n"
        "        id: upload-unsigned\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        f"          name: {installer_artifact}-unsigned\n"
        f"          path: installer/{installer_file}\n"
        "          overwrite: true\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("Windows installer signing upload of the installer alone, replaceable on a re-run, as the artifact that the signing request names")
    installer_request = step_block(installer_job, "Sign the installer")
    if any(
        part not in installer_request
        for part in (
            "        uses: signpath/github-action-submit-signing-request@v3\n",
            "          api-token: ${{ secrets.SIGNPATH_API_TOKEN }}\n",
            "          artifact-configuration-slug: ${{ vars.SIGNPATH_INSTALLER_ARTIFACT_CONFIGURATION_SLUG }}\n",
            "          github-artifact-id: ${{ steps.upload-unsigned.outputs.artifact-id }}\n",
            "          wait-for-completion: true\n",
            "          output-artifact-directory: signed\n",
        )
    ):
        missing.append("Windows installer signing request for the uploaded installer with its own artifact configuration")
    installer_verify_env = (
        "        shell: pwsh\n"
        "        env:\n"
        "          EXPECTED_SUBJECT: ${{ vars.WINDOWS_SIGNING_SUBJECT }}\n"
    )
    if step_block(installer_job, "Verify the program files") != (
        "name: Verify the program files\n" + installer_verify_env +
        "        run: ./tools/release/verify_signatures.ps1 -Directory payload -Require -ExpectedSubject $env:EXPECTED_SUBJECT\n"
    ) or step_block(installer_job, "Verify the installer") != (
        "name: Verify the installer\n" + installer_verify_env +
        f'        run: ./tools/release/verify_signatures.ps1 -Directory signed -Name "{installer_file}" -Require '
        "-ExpectedSubject $env:EXPECTED_SUBJECT\n"
    ):
        missing.append(
            "Windows installer signing check that requires the expected signer on QEGTRAIN.exe, egtrain_update_helper.exe, "
            "scene_tool.exe and the signed installer, each in a step of its own so that one failure fails the job"
        )
    if step_block(installer_job, "Upload the signed installer") != (
        "name: Upload the signed installer\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        f"          name: {installer_artifact}\n"
        f"          path: signed/{installer_file}\n"
        "          overwrite: true\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("Windows installer signing upload of the checked installer in place of the installer artifact")
    # The release job takes the package workflow as a whole, so it runs only after every job of that workflow, the signing and
    # the checks of the signatures included, succeeded or was skipped. A condition with a status function, or a step that
    # ignores a failure, would let it publish files that were not checked.
    if (
        re.findall(r"^    needs: (.+)$", release_job, re.MULTILINE) != ["[version, validation, sanitizer, package]"]
        or re.findall(r"^    if: (.+)$", release_job, re.MULTILINE)
        != ["github.event_name == 'push' && (github.ref == 'refs/heads/production' || startsWith(github.ref, 'refs/tags/v'))"]
        or "continue-on-error" in release_only
        or "continue-on-error" in package_only
    ):
        missing.append("release job that publishes only after the whole package workflow succeeded, with no failure ignored")
    # The smoke script cannot run in this test. It has to keep the silent switches, the directory, the desktop task and
    # the calls of its start, manifest, registry, upgrade and uninstall checks, and no hash, no Program Files and no
    # /CURRENTUSER, which has no effect while the installer script offers no choice of scope.
    installer_smoke = (ROOT / "tools/release/installer_smoke.ps1").read_text(encoding="utf-8")
    if any(
        part not in installer_smoke
        for part in (
            "'/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'",
            '/LOG=`"$log`"',
            '/DIR=`"$Directory`"',
            "'/TASKS=desktopicon'",
            "Assert-AsInvoker $Installer",
            "Assert-AsInvoker $UpgradeInstaller",
            "Invoke-StartSmoke $directory",
            "Install-Egtrain $UpgradeInstaller 'upgrade-over'",
            "Assert-Registered $defaultDirectory $UpgradeVersion",
            "Assert-Survives $outside",
        )
    ) or any(part in installer_smoke.lower() for part in ("/currentuser", "/allusers", "sha256", "get-filehash", "program files")):
        missing.append("installer smoke script with the silent switches, the desktop task, the start check and the checks of scope and manifest")
    # The application loads no QML, WebP or TIFF file and takes keyboard input from the system. The macOS package job removes
    # the virtual keyboard plugin with the Quick and QML frameworks and the WebP and TIFF plugins with their libraries after
    # macdeployqt and before the signature, fails when one of them is in the app, and starts a copy of the app. The
    # app holds scene_tool and the Lebanon guide, and the disk image is built from it after the one signature.
    macos_job = job_block(package_only, "package-macos")
    macos_steps = (
        "Test the notarization script",
        "Bundle Qt and dependencies into the app",
        "Add scene_tool and the Lebanon guide to the app",
        "Remove unused Qt modules and plugins",
        "Set the minimum macOS version",
        "Import the signing certificate",
        "Sign the app",
        "Verify the app is self-contained",
        "Verify the signature",
        "Verify the packaged app",
        "Start the package",
        "Notarize the app",
        "Build the disk image",
        "Sign the disk image",
        "Notarize the disk image",
        "Verify the disk image",
        "Upload artifact",
        "Delete the signing keychain",
    )
    macos_step_names = re.findall(r"^      - name: (.+)$", macos_job, re.MULTILINE)
    macos_positions = [macos_step_names.index(name) if name in macos_step_names else -1 for name in macos_steps]
    if -1 in macos_positions or macos_positions != sorted(macos_positions):
        missing.append("macOS package steps in the order " + ", ".join(macos_steps))
    removal_step = step_block(macos_job, "Remove unused Qt modules and plugins")
    trim_start = removal_step.find('          report_size "before trimming"\n')
    trim_end = removal_step.find('          report_size "after trimming"\n')
    if (
        "          report_size() {\n" not in removal_step
        or '            echo "macOS app $1: $files files, $bytes bytes" | tee -a "$GITHUB_STEP_SUMMARY"\n' not in removal_step
        or not 0 <= trim_start < trim_end
    ):
        missing.append("macOS package size lines in the job summary before and after the removal")
    trimming = removal_step[trim_start:trim_end] if 0 <= trim_start < trim_end else ""
    trimming = [line.strip() for line in trimming.replace("\\\n", " ").splitlines() if not line.lstrip().startswith("#")]
    removed_paths = (
        '"$APP/Contents/PlugIns/virtualkeyboard"',
        '"$APP/Contents/PlugIns/platforminputcontexts"',
        '"$APP/Contents/PlugIns/imageformats/libqwebp.dylib"',
        '"$APP/Contents/PlugIns/imageformats/libqtiff.dylib"',
        '"$APP/Contents/Frameworks/QtVirtualKeyboard.framework"',
        '"$APP/Contents/Frameworks/QtQuick.framework"',
        '"$APP/Contents/Frameworks/QtQml.framework"',
        '"$APP/Contents/Frameworks/QtQmlModels.framework"',
        '"$APP"/Contents/Frameworks/libwebp*.dylib',
        '"$APP"/Contents/Frameworks/libtiff*.dylib',
        '"$APP"/Contents/Frameworks/liblzma*.dylib',
    )
    for removed in removed_paths:
        if trimming and not any(line.startswith("rm -") and removed in line for line in trimming):
            missing.append(f"macOS package step that removes {removed} between the two size lines")
    # The cocoa platform plugin loads QtDBus and QtPrintSupport, so they stay, as do the print support and bearer plugins.
    removal_commands = [line for line in removal_step.splitlines() if not line.lstrip().startswith("#")]
    if any(kept in line for line in removal_commands for kept in ("QtDBus", "QtPrintSupport", "printsupport", "bearer")):
        missing.append("macOS package step that keeps QtDBus, QtPrintSupport and the print support and bearer plugins")
    macos_verify = step_block(macos_job, "Verify the app is self-contained")
    required_plugins = (
        "          for plugin in platforms/libqcocoa.dylib iconengines/libqsvgicon.dylib styles/libqmacstyle.dylib; do\n"
        '            test -f "$APP/Contents/PlugIns/$plugin" || { echo "ERROR: missing $plugin"; exit 1; }\n'
        "          done\n"
    )
    forbidden_macos_files = (
        "          for name in virtualkeyboard platforminputcontexts 'QtVirtualKeyboard*' 'QtQuick*' 'QtQml*' 'libqwebp*' 'libqtiff*'"
        " 'libwebp*' 'libtiff*'; do\n"
        '            found="$(find "$APP" -name "$name")"\n'
        '            if [ -n "$found" ]; then echo "ERROR: unexpected $name in the app:"; echo "$found"; exit 1; fi\n'
        "          done\n"
    )
    macos_verified = macos_verify.find('          echo "OK: app is self-contained and signed, without the virtual keyboard, QML, WebP and TIFF files"\n')
    if macos_verified < 0:
        missing.append("macOS package check that ends with a message that names the files it forbids")
    if required_plugins not in macos_verify or not 0 <= macos_verify.find(required_plugins) < macos_verified:
        missing.append("macOS package check that requires the cocoa platform plugin, the SVG icon engine and the macOS style")
    if forbidden_macos_files not in macos_verify or not 0 <= macos_verify.find(forbidden_macos_files) < macos_verified:
        missing.append("macOS package check that forbids the virtual keyboard, QML, WebP and TIFF files")
    macos_start = step_block(macos_job, "Start the package")
    macos_python = next((block for block in macos_job.split("\n      - ") if block.startswith("uses: actions/setup-python@v5\n")), "")
    if (
        not macos_start.strip().endswith('python3 tools/release/package_start_smoke.py "$start"')
        or 'start="$RUNNER_TEMP/package-start"\n' not in macos_start
        or 'ditto build/QEGTRAIN.app "$start/QEGTRAIN.app"\n' not in macos_start
        or 'PYTHONUTF8: "1"' not in macos_start
        or "python-version: '3.12'" not in macos_python
        or not 0 <= macos_job.find("uses: actions/setup-python@v5") < macos_job.find("name: Start the package")
    ):
        missing.append("macOS package start of a copy under RUNNER_TEMP with Python set up")
    # scene_tool and the guide go into the app before the signature, which seals them, and not into a folder beside it. The
    # app is built for macOS 26 and says so in its property list, in the same step order.
    add_step = step_block(macos_job, "Add scene_tool and the Lebanon guide to the app")
    if (
        '          cp build/scene_tool "$APP/Contents/MacOS/scene_tool"\n' not in add_step
        or '          cp docs/guides/lebanon-case-study.md "$APP/Contents/Resources/lebanon-case-study.md"\n' not in add_step
    ):
        missing.append("macOS package step that copies scene_tool and the Lebanon guide into the app")
    if "          -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0\n" not in step_block(macos_job, "Configure"):
        missing.append("macOS package configure for the deployment target macOS 26")
    minimum_step = step_block(macos_job, "Set the minimum macOS version")
    if (
        "otool -l \"$APP/Contents/MacOS/QEGTRAIN\"" not in minimum_step
        or 'PlistBuddy -c "Add :LSMinimumSystemVersion string $minimum" "$APP/Contents/Info.plist"\n' not in minimum_step
    ):
        missing.append("macOS package step that writes the minimum system version of the executable into the property list")
    # The image is the macOS package and the only macOS artifact. The script builds it after every step that runs or changes
    # the app, and no step after it uses build/QEGTRAIN.app: the image is mounted read-only and detached on every path, and
    # the app is checked as the copy that ditto takes out of it.
    if "zip" in macos_job.lower() or "build/dist" in macos_job or "QEGTRAIN-Lebanon" in release_workflow:
        missing.append("macOS package without a folder of the presentation package and without an archive")
    image_step = step_block(macos_job, "Build the disk image")
    if not image_step.strip().endswith('run: python3 tools/release/make_dmg.py --version "${{ inputs.version }}" --app build/QEGTRAIN.app --output build/dmg --layout'):
        missing.append("macOS disk image built by the script from the final app")
    after_image = macos_job.split("      - name: Verify the disk image\n", 1)[-1]
    if "build/QEGTRAIN.app" in after_image or any(
        change in after_image for change in ("codesign --force", "install_name_tool", "macdeployqt", "chmod", "PlistBuddy -c \"Add")
    ):
        missing.append("macOS app left as signed after the disk image is built")
    image_name = make_dmg.dmg_name("${{ inputs.version }}")
    image_verify = step_block(macos_job, "Verify the disk image")
    verification_in_order = (
        f'DMG="$PWD/build/dmg/{image_name}"\n',
        "trap 'hdiutil detach \"$MOUNT\" -force >/dev/null 2>&1 || true' EXIT\n",
        'hdiutil attach -readonly -nobrowse -noautoopen -mountpoint "$MOUNT" "$DMG"\n',
        'test "$(LC_ALL=C ls -A "$MOUNT" | tr \'\\n\' \' \')" = ".DS_Store Applications QEGTRAIN.app "\n',
        'test "$(readlink "$MOUNT/Applications")" = "/Applications"\n',
        'cmp "$MOUNT/.DS_Store" tools/release/dmg/DS_Store\n',
        'codesign --verify --deep --strict --verbose=2 "$MOUNT/QEGTRAIN.app"\n',
        'ditto "$MOUNT/QEGTRAIN.app" "$COPY/QEGTRAIN.app"\n',
        'hdiutil detach "$MOUNT" || hdiutil detach "$MOUNT" -force\n          trap - EXIT\n',
        'codesign --verify --deep --strict --verbose=2 "$COPY/QEGTRAIN.app"\n',
        '[ "$version" = "${{ inputs.version }}" ]',
        '"$COPY/QEGTRAIN.app/Contents/MacOS/QEGTRAIN" --scene "$COPY/QEGTRAIN.app/Contents/Resources/Scenes/Paimpol" -h 300 -g 0 -pax 0 -TSM 0 -RC 0 >"$RUN_DIR/qegtrain.log" 2>&1\n',
        'grep -q "End of Simulation" "$RUN_DIR/qegtrain.log"',
        'test -f "$RUN_DIR/Output/Paimpol/EnergyConsumptionPerTrain.txt"',
    )
    verification_positions = [image_verify.find(check) for check in verification_in_order]
    if -1 in verification_positions or verification_positions != sorted(verification_positions):
        missing.append("macOS disk image check that mounts it read-only, detaches it on every path, verifies the signature of the app in it and of a copy, and runs the copy")
    # Finder takes the document type from the property list of the app in the image: .egscene is the one type of the app and
    # conforms to public.data only. The checks read the mounted app, between its signature and the copy.
    document_type_checks = (
        'codesign --verify --deep --strict --verbose=2 "$MOUNT/QEGTRAIN.app"\n',
        'PLIST="$MOUNT/QEGTRAIN.app/Contents/Info.plist"\n',
        'expect_plist CFBundleDocumentTypes:0:CFBundleTypeName "EGTRAIN Case Study"\n',
        "expect_plist CFBundleDocumentTypes:0:CFBundleTypeRole Editor\n",
        "expect_plist CFBundleDocumentTypes:0:LSHandlerRank Owner\n",
        "expect_plist CFBundleDocumentTypes:0:LSItemContentTypes:0 com.egtrain.simulator.egscene\n",
        "expect_no_plist CFBundleDocumentTypes:0:LSItemContentTypes:1\n",
        "expect_no_plist CFBundleDocumentTypes:1\n",
        "expect_plist UTExportedTypeDeclarations:0:UTTypeIdentifier com.egtrain.simulator.egscene\n",
        "expect_plist UTExportedTypeDeclarations:0:UTTypeConformsTo:0 public.data\n",
        "expect_no_plist UTExportedTypeDeclarations:0:UTTypeConformsTo:1\n",
        "expect_plist UTExportedTypeDeclarations:0:UTTypeTagSpecification:public.filename-extension:0 egscene\n",
        "expect_no_plist UTExportedTypeDeclarations:0:UTTypeTagSpecification:public.filename-extension:1\n",
        "expect_no_plist UTExportedTypeDeclarations:1\n",
        'ditto "$MOUNT/QEGTRAIN.app" "$COPY/QEGTRAIN.app"\n',
    )
    document_type_positions = [image_verify.find(check) for check in document_type_checks]
    if -1 in document_type_positions or document_type_positions != sorted(document_type_positions):
        missing.append("macOS disk image check of the .egscene document type and its type declaration in the property list of the mounted app")
    if any(
        f'test -x "$COPY/QEGTRAIN.app/Contents/MacOS/{program}"\n' not in image_verify for program in ("QEGTRAIN", "egtrain_update_helper", "scene_tool")
    ) or 'test -f "$COPY/QEGTRAIN.app/Contents/Resources/lebanon-case-study.md"\n' not in image_verify:
        missing.append("macOS disk image check of the programs and the guide in the copy of the app")
    if step_block(macos_job, "Upload artifact") != (
        "name: Upload artifact\n"
        "        uses: actions/upload-artifact@v4\n"
        "        with:\n"
        "          name: QEGTRAIN-macos-arm64\n"
        f"          path: build/dmg/{image_name}\n"
        "          if-no-files-found: error\n"
    ):
        missing.append("macOS upload of the disk image as the only file of the artifact that the release script reads")
    # The script signs the app: ad hoc without an identity, and with the Developer ID identity when the environment
    # macos-signing holds a certificate. Only a push to production or a v* tag runs in that environment, the import step is the
    # one place that reads the certificate and its password, the identity reaches the later steps only after the import
    # worked, and the keychain of the import is deleted by the last step on every path.
    signing_gate = "github.event_name == 'push' && (github.ref == 'refs/heads/production' || startsWith(github.ref, 'refs/tags/v'))"
    if re.findall(r"^    environment: (.+)$", macos_job, re.MULTILINE) != ["${{ " + signing_gate + " && 'macos-signing' || '' }}"]:
        missing.append("macOS package job that runs in the environment macos-signing for a push to production or a v* tag, and in no environment otherwise")
    import_step = step_block(macos_job, "Import the signing certificate")
    import_run = import_step.split("        run: |\n", 1)[-1]
    if (
        "        if: ${{ " + signing_gate + " }}\n" not in import_step
        or "          MACOS_CERTIFICATE: ${{ secrets.MACOS_CERTIFICATE }}\n" not in import_step
        or "          MACOS_CERTIFICATE_PASSWORD: ${{ secrets.MACOS_CERTIFICATE_PASSWORD }}\n" not in import_step
        or "          IDENTITY: ${{ vars.MACOS_SIGNING_IDENTITY }}\n" not in import_step
        or re.search(r"\bset\s+-\w*x", import_step)
        or not 0 <= import_run.find('if [ -z "$MACOS_CERTIFICATE" ]; then\n') < import_run.find("            exit 0\n") < import_run.find("security create-keychain")
        or not 0 <= import_run.find("security create-keychain") < import_run.find('echo "MACOS_KEYCHAIN=$keychain" >> "$GITHUB_ENV"\n')
        or not 0 <= import_run.find("security import") < import_run.find("security find-identity -v -p codesigning")
        or not import_run.find("security find-identity -v -p codesigning") < import_run.find('echo "MACOS_SIGNING_IDENTITY=$IDENTITY" >> "$GITHUB_ENV"\n')
        or import_run.count("MACOS_SIGNING_IDENTITY=$IDENTITY") != 1
        or "trap 'rm -f \"$certificate\"' EXIT\n" not in import_run
    ):
        missing.append("macOS signing certificate import that runs for a push to production or a v* tag, does nothing without a certificate, and passes on the identity only after the import")
    sign_step = step_block(macos_job, "Sign the app")
    if (
        not sign_step.endswith("        run: bash tools/release/sign_macos.sh build/QEGTRAIN.app\n")
        or "env:" in sign_step
        or package_only.count("codesign --force") != 1
        or "--deep --sign" in package_only
    ):
        missing.append("macOS app signed by the script alone, with the identity from the environment of the import step and no codesign call in the workflow but the one for the disk image")
    signature_step = step_block(macos_job, "Verify the signature")
    if any(
        part not in signature_step
        for part in (
            'details="$(codesign -dvv build/QEGTRAIN.app 2>&1)"\n',
            "expect '^Authority=Developer ID Application: '",
            "expect '^TeamIdentifier=[A-Z0-9]{10}$'",
            "expect '^CodeDirectory .*flags=.*runtime'",
            "expect '^Timestamp='",
            "expect '^Signature=adhoc$'",
        )
    ):
        missing.append("macOS signature check that prints who signed the app and requires Developer ID, a team, the hardened runtime and a timestamp, or an ad-hoc signature without an identity")
    # Notarization follows the signing. The app is notarized and stapled before the disk image is built from it, so that its
    # ticket is in the image. The image is signed with the identity of the app, if there is one, and then notarized. Both
    # notarization steps run only where the signing steps run, read the notarization credentials of the environment in the
    # step alone, and fail the job when the script fails: the script itself does nothing without credentials, so no step
    # condition and no continue-on-error is needed or allowed.
    image_sign_step = step_block(macos_job, "Sign the disk image")
    image_sign_run = image_sign_step.split("        run: |\n", 1)[-1]
    if (
        "env:" in image_sign_step
        or "--deep" in image_sign_step
        or f'DMG="build/dmg/{image_name}"\n' not in image_sign_run
        or 'codesign --force --timestamp --sign "$MACOS_SIGNING_IDENTITY" "$DMG"\n' not in image_sign_run
        or not 0
        <= image_sign_run.find('if [ -z "${MACOS_SIGNING_IDENTITY:-}" ]; then\n')
        < image_sign_run.find("            exit 0\n")
        < image_sign_run.find("codesign --force")
        < image_sign_run.find('codesign --verify --verbose=2 "$DMG"\n')
    ):
        missing.append("macOS disk image signed with the identity of the app, and left unsigned without an identity")
    notary_environment = (
        "        env:\n"
        "          MACOS_NOTARY_KEY: ${{ secrets.MACOS_NOTARY_KEY }}\n"
        "          MACOS_NOTARY_KEY_ID: ${{ vars.MACOS_NOTARY_KEY_ID }}\n"
        "          MACOS_NOTARY_ISSUER_ID: ${{ vars.MACOS_NOTARY_ISSUER_ID }}\n"
        "          MACOS_NOTARY_APPLE_ID: ${{ vars.MACOS_NOTARY_APPLE_ID }}\n"
        "          MACOS_NOTARY_TEAM_ID: ${{ vars.MACOS_NOTARY_TEAM_ID }}\n"
        "          MACOS_NOTARY_PASSWORD: ${{ secrets.MACOS_NOTARY_PASSWORD }}\n"
    )
    for notary_name, notary_target in (
        ("Notarize the app", "build/QEGTRAIN.app"),
        ("Notarize the disk image", f"build/dmg/{image_name}"),
    ):
        notary_step = step_block(macos_job, notary_name)
        if (
            "        if: ${{ " + signing_gate + " }}\n" not in notary_step
            or "        timeout-minutes: 60\n" not in notary_step
            or notary_environment not in notary_step
            or not notary_step.endswith(f"        run: bash tools/release/notarize_macos.sh {notary_target}\n")
        ):
            missing.append(f"macOS step '{notary_name}' that runs the notarization script for a push to production or a v* tag with the notarization credentials of its environment")
    step_conditions = re.findall(r"^        if: (.+)$", macos_job, re.MULTILINE)
    if "continue-on-error" in macos_job or any(re.search(r"secrets\.|vars\.|NOTARY|CERTIFICATE", condition) for condition in step_conditions):
        missing.append("macOS package job whose steps all fail the job and whose step conditions read no credential")
    notarize_script = (ROOT / "tools/release/notarize_macos.sh").read_text(encoding="utf-8")
    notarize_commands = ("xcrun notarytool submit", "--wait", "xcrun stapler staple", "xcrun stapler validate", "spctl --assess")
    notarize_positions = [notarize_script.find(command) for command in notarize_commands]
    printed_credentials = [
        line for line in notarize_script.splitlines() if re.search(r"\b(echo|printf|fail)\b.*\$\{?MACOS_NOTARY_", line) and "key.p8" not in line
    ]
    if (
        -1 in notarize_positions
        or notarize_positions != sorted(notarize_positions)
        or re.search(r"\bset\s+-\w*x", notarize_script)
        or printed_credentials
    ):
        missing.append("notarization script that submits to Apple and waits, staples, validates and assesses in that order, and neither traces nor prints a credential")
    if re.findall(r"^      - name: (.+)$", macos_job, re.MULTILINE)[-1] != "Delete the signing keychain" or step_block(
        macos_job, "Delete the signing keychain"
    ) != (
        "name: Delete the signing keychain\n"
        "        if: ${{ always() && env.MACOS_KEYCHAIN != '' }}\n"
        '        run: security delete-keychain "$MACOS_KEYCHAIN"\n'
    ):
        missing.append("macOS package job whose last step deletes the keychain of the certificate import on every path")
    # Gatekeeper is never bypassed: no workflow and no release script takes the quarantine flag off a file or turns the check off.
    bypass = re.compile(r"\bxattr\b(?:\\\n|[^\n;|&])*(?:[\s'\"]-\w*[cd]|com\.apple\.quarantine)|\bspctl\s+--(master|global)-disable")
    bypassing = [
        path.name
        for path in sorted((ROOT / ".github/workflows").glob("*.yml")) + sorted((ROOT / "tools/release").glob("*.*"))
        if path.suffix in (".yml", ".sh", ".py", ".ps1") and bypass.search(path.read_text(encoding="utf-8"))
    ]
    if bypassing:
        missing.append("no step that removes the quarantine attribute or turns Gatekeeper off, found in " + ", ".join(bypassing))
    if (
        "\t\tadd_test(NAME test_sign_macos\n\t\t\tCOMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_sign_macos.sh)\n"
        not in cmake.split("\tif(APPLE)\n\t\tadd_test(NAME test_package_contents_smoke", 1)[-1].split("\tendif()", 1)[0]
        or not re.search(r"egtrain_label_tests\(integration\b[^)]*\btest_sign_macos\b", cmake)
    ):
        missing.append("signing script test registered in CTest on macOS only, with the integration label")
    package_check_paths = (
        ".github/workflows/package-check.yml",
        ".github/workflows/package.yml",
        ".github/workflows/release.yml",
        "tools/release/**",
        "installer/**",
        "Info.plist.in",
        "EGTRAIN/QEGTRAIN/update/**",
        "EGTRAIN/QEGTRAIN/app/main.cpp",
    )
    package_check_trigger = trigger_block(package_check)
    package_check_pull_request = re.search(r"^  pull_request:\n((?:    .*\n)*)", package_check_trigger, re.MULTILINE)
    if (
        trigger_events(package_check) != ["pull_request", "schedule", "workflow_dispatch"]
        or not package_check_pull_request
        or package_check_pull_request.group(1)
        != "    branches: [main]\n    paths:\n" + "".join(f"      - '{path}'\n" for path in package_check_paths)
    ):
        missing.append("package check starts on pull requests to main only for a packaging input, and never for a push")
    package_check_nightly = re.search(
        r"^  schedule:\n    - cron: '(\d+) (\d+) \* \* \*'\n(?!    -)", package_check_trigger, re.MULTILINE
    )
    if (
        not package_check_nightly
        or package_check_nightly.group(1) == "0"
        or not re.search(r"^  workflow_dispatch:\n(?!    )", package_check_trigger, re.MULTILINE)
    ):
        missing.append("package check with one nightly run off the hour and a manual start without inputs")
    if (
        "\npermissions:\n  contents: read\n" not in package_check
        or package_check.count("permissions:") != 2
        or re.search(r":\s*write", package_check)
    ):
        missing.append("package check with read permission only, and the block of its package job")
    if (
        "\nconcurrency:\n"
        "  group: package-check-${{ github.event_name == 'pull_request' && github.ref || github.run_id }}\n"
        "  cancel-in-progress: ${{ github.event_name == 'pull_request' }}\n" not in package_check
    ):
        missing.append("package check replaces only the older run of the same pull request")
    if job_ids(package_check) != ["version", "package", "release-assets"]:
        missing.append("package check jobs: version, package, release-assets")
    if (
        job_block(package_check, "version") != job_block(release_only, "version")
        or "run: python3 tools/release/version.py\n" not in package_check
    ):
        missing.append("package check selects the version with the script and job of the release workflow")
    if job_block(package_check, "package") != "  package:\n" + package_call:
        missing.append("package check calls the package workflow with the selected version and the two read permissions, and no token")
    # The release assets job runs the script that builds the files of a release on the artifacts of the package
    # jobs. It uploads and publishes nothing, and it has the permissions of the workflow. Its download and script steps
    # are those of the release job, so the package check runs what a release runs.
    download_step = (
        "name: Download all artifacts\n"
        "        uses: actions/download-artifact@v4\n"
        "        with:\n"
        "          path: artifacts"
    )
    build_step = (
        "name: Build the release assets\n"
        "        env:\n"
        "          VERSION: ${{ needs.version.outputs.version }}\n"
        '        run: python3 tools/release/build_release_assets.py --version "$VERSION" --artifacts artifacts --output release-assets\n'
    )
    assets_job = job_block(package_check, "release-assets")
    if (
        re.findall(r"^    needs: (.+)$", assets_job, re.MULTILINE) != ["[version, package]"]
        or re.findall(r"^    runs-on: (.+)$", assets_job, re.MULTILINE) != ["ubuntu-latest"]
        or "      - uses: actions/checkout@v4\n" not in assets_job
    ):
        missing.append("package check release assets job after the version and package jobs, on ubuntu-latest with a checkout")
    # The release job runs the script from its checkout, so the checkout has to come first.
    if not 0 <= release_job.find("      - uses: actions/checkout@v4\n") < release_job.find("      - name: Build the release assets\n"):
        missing.append("release job checkout before the step that builds the release assets")
    if re.findall(r"^      - name: (.+)$", assets_job, re.MULTILINE) != [
        "Download all artifacts",
        "List downloaded files",
        "Build the release assets",
        "List the release assets",
    ]:
        missing.append("package check release assets steps: download, list, build, list")
    if step_block(assets_job, "Download all artifacts").rstrip("\n") != download_step:
        missing.append("package check release assets download of all artifacts into artifacts")
    if step_block(assets_job, "Build the release assets") != build_step:
        missing.append("package check release assets script run with the selected version")
    if step_block(release_job, "Download all artifacts") != step_block(assets_job, "Download all artifacts") or (
        step_block(release_job, "Download all artifacts").rstrip("\n") != download_step
    ):
        missing.append("release job download of all artifacts into artifacts, as the package check does")
    if step_block(release_job, "Build the release assets") != build_step or (
        step_block(release_job, "Build the release assets") != step_block(assets_job, "Build the release assets")
    ):
        missing.append("release job script run with the selected version, with the command line of the package check")
    if not assets_job or any(word in assets_job for word in ("upload-artifact", "permissions:", "if:")):
        missing.append("package check release assets job without an upload, a permissions block or a condition")
    if (
        "add_test(NAME test_release_assets\n"
        "\t\tCOMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_release_assets.py)\n" not in cmake
        or not re.search(r"egtrain_label_tests\(unit\b[^)]*\btest_release_assets\b", cmake)
    ):
        missing.append("release assets unit test registered in CTest with the unit label")
    # The Linux package job extracts the AppImage that it built into a directory under RUNNER_TEMP and starts it through its
    # AppRun on a virtual display, with the platform plugin that the image holds. Every package job starts its package with
    # the same script.
    linux_job = job_block(package_only, "package-linux")
    linux_steps = ("Build AppImage", "Verify AppImage contents", "Start the package", "Upload artifact")
    linux_step_names = re.findall(r"^      - name: (.+)$", linux_job, re.MULTILINE)
    linux_positions = [linux_step_names.index(name) if name in linux_step_names else -1 for name in linux_steps]
    if -1 in linux_positions or linux_positions != sorted(linux_positions):
        missing.append("Linux package steps in the order " + ", ".join(linux_steps))
    linux_install = re.search(r"apt-get install -y((?:.*\\\n)*.*\n)", step_block(linux_job, "Install dependencies"))
    linux_apt_packages = linux_install.group(1).replace("\\", " ").split() if linux_install else []
    if "xvfb" not in linux_apt_packages or "xauth" not in linux_apt_packages:
        missing.append("Linux package job that installs xvfb and xauth for the virtual display")
    linux_start = step_block(linux_job, "Start the package")
    linux_python = next((block for block in linux_job.split("\n      - ") if block.startswith("uses: actions/setup-python@v5\n")), "")
    if (
        not linux_start.strip().endswith(
            'xvfb-run -a -s "-screen 0 1920x1080x24" python3 tools/release/package_start_smoke.py "$start/squashfs-root"')
        or 'PYTHONUTF8: "1"' not in linux_start
        or 'start="$RUNNER_TEMP/package-start"\n' not in linux_start
        or '(cd "$start" && "$image" --appimage-extract >/dev/null)\n' not in linux_start
        or 'find "$start/squashfs-root/usr/plugins" -type f | sort\n' not in linux_start
        or "python-version: '3.12'" not in linux_python
        or not 0 <= linux_job.find("uses: actions/setup-python@v5") < linux_job.find("name: Start the package")
    ):
        missing.append("Linux package start of the extracted AppImage under RUNNER_TEMP on a virtual display with Python set up")
    if "QT_QPA_PLATFORM" in linux_job or "QT_PLUGIN_PATH" in linux_job:
        missing.append("Linux package start with the platform plugin that the image holds, without a Qt variable")
    if package_only.count("tools/release/package_start_smoke.py") != 4:
        missing.append("package start script in the three package jobs and for the portable archive")
    publishing = ("action-gh-release", "gh release", "gh api", "git push", "git commit")
    if (
        any(word in text for text in (package_check, package_only) for word in publishing)
        or "codesign" in package_check
        or "secrets" in package_check
    ):
        missing.append("no publishing, signing or repository write in the package check and the package workflow")
    if missing:
        raise SystemExit("CI workflows are missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
