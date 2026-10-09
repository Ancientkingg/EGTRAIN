#!/usr/bin/env python3
import re
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
    if "\n  sanitizer:" in workflow or "EGTRAIN_ENABLE_SANITIZERS=ON" in workflow:
        missing.append("sanitizer job")
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
            ('version="${{ needs.version.outputs.version }}"', release_only),
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
        or "needs." in package_only
    ):
        missing.append("quoted shared release version in all five build jobs")
    if release_only.count("    needs: version\n") != 3 or package_only.count("needs:") != 0:
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
    macos_package_verification = release_workflow.split(
        "      - name: Verify the presentation package\n", 1
    )[1].split("\n      - ", 1)[0]
    required_macos_validation = (
        "set +e",
        'validation_output="$("$PKG/scene_tool" validate "$PKG/Scenes/Lebanon" 2>&1)"',
        "validation_status=$?",
        "set -e",
        '[ "$validation_status" -eq 0 ]',
    )
    if (
        any(check not in macos_package_verification for check in required_macos_validation)
        or '"$PKG/scene_tool" validate "$PKG/Scenes/Lebanon" 2>&1 | grep' in macos_package_verification
    ):
        missing.append("macOS scene validation status handling")
    required_macos_package_checks = (
        'APP="$(cd "$PKG/QEGTRAIN.app" && pwd)"',
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
    app_path_index = macos_package_verification.find('APP="$(cd "$PKG/QEGTRAIN.app" && pwd)"')
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
    if any(
        f"artifacts/EGTRAIN-scenes/{name}.egscene" not in release_workflow
        for name in scene_names
    ) or "artifacts/EGTRAIN-scenes/*.egscene" not in release_workflow:
        missing.append("scene bundles in published release assets")
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
    if any(
        value not in release_workflow
        for value in (
            "update-manifest.json",
            'QEGTRAIN-macos-arm64.zip",sha256:$mac_sha',
            'QEGTRAIN-windows-x64.zip",sha256:$windows_sha',
            'QEGTRAIN-linux-x86_64.AppImage",sha256:$linux_sha',
            'if [[ "$count" != "11" ]]',
        )
    ):
        missing.append("release update manifest and exact package checksums")
    if any(
        value not in release_workflow
        for value in (
            "unzip -Z1 artifacts/QEGTRAIN-windows-x64/QEGTRAIN-windows-x64.zip | tr '\\\\' '/' | grep -v '/$'",
            "jq -e 'index(\"QEGTRAIN.exe\")' <<< \"$windows_files\"",
            '--argjson windows_files "$windows_files"',
            "size:$windows_size,files:$windows_files}",
        )
    ):
        missing.append("Windows package file list in the update manifest")
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
    apt_install = re.search(r"apt-get install -y((?:.*\\\n)*.*\n)", workflow)
    apt_packages = apt_install.group(1).replace("\\", " ").split() if apt_install else []
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
    # The package jobs live in a reusable workflow that the release workflow and the package check call.
    version_call = (
        "    needs: version\n"
        "    uses: ./.github/workflows/package.yml\n"
        "    with:\n"
        "      version: ${{ needs.version.outputs.version }}\n"
    )
    if (
        trigger_events(release_only) != ["push", "pull_request", "workflow_dispatch"]
        or "\nconcurrency:\n"
        "  group: ${{ github.event_name == 'push' && 'egtrain-publish' || github.run_id }}\n"
        "  cancel-in-progress: false\n\njobs:\n" not in release_only
        or release_only.count("permissions:") != 1
    ):
        missing.append("release triggers, concurrency and permissions")
    if job_ids(release_only) != ["version", "validation", "sanitizer", "package", "release"]:
        missing.append("release jobs: version, validation, sanitizer, package, release")
    if job_block(release_only, "package") != "  package:\n" + version_call:
        missing.append("release package job calls the package workflow with the selected version")
    if trigger_block(package_only) != (
        "  workflow_call:\n    inputs:\n      version:\n        required: true\n        type: string\n"
    ):
        missing.append("package workflow starts only by call, with the version as its only input")
    if (
        "\npermissions:\n  contents: read\n\njobs:\n" not in package_only
        or package_only.count("permissions:") != 1
        or "secrets" in package_only
    ):
        missing.append("package workflow with read permission and no credentials")
    if job_ids(package_only) != ["package-macos", "package-windows", "package-linux"]:
        missing.append("package workflow jobs: package-macos, package-windows, package-linux")
    for artifact, file in (
        ("QEGTRAIN-macos-arm64", "QEGTRAIN-macos-arm64.zip"),
        ("QEGTRAIN-windows-x64", "QEGTRAIN-windows-x64.zip"),
        ("QEGTRAIN-linux-x86_64", "QEGTRAIN-linux-x86_64.AppImage"),
    ):
        if (
            f"          name: {artifact}\n          path: " not in package_only
            or release_only.count(f"            artifacts/{artifact}/{file}\n") != 2
        ):
            missing.append(f"package artifact {artifact} downloaded by the release job")
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
    windows_steps = ("Assemble package", "Verify the package is complete", "Report package size", "Start the package", "Zip", "Upload artifact")
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
    if "Compress-Archive -Path dist/QEGTRAIN/* -DestinationPath QEGTRAIN-windows-x64.zip" not in step_block(windows_job, "Zip"):
        missing.append("Windows package zip of the directory that is built")
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
        or package_check.count("permissions:") != 1
        or re.search(r":\s*write", package_check)
    ):
        missing.append("package check with read permission only")
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
    if job_block(package_check, "package") != "  package:\n" + version_call:
        missing.append("package check calls the package workflow with the selected version")
    # The release assets job runs the script that builds the files of a release on the artifacts of the package
    # jobs. It uploads and publishes nothing, and it has the permissions of the workflow.
    assets_job = job_block(package_check, "release-assets")
    if (
        re.findall(r"^    needs: (.+)$", assets_job, re.MULTILINE) != ["[version, package]"]
        or re.findall(r"^    runs-on: (.+)$", assets_job, re.MULTILINE) != ["ubuntu-latest"]
        or "      - uses: actions/checkout@v4\n" not in assets_job
    ):
        missing.append("package check release assets job after the version and package jobs, on ubuntu-latest with a checkout")
    if re.findall(r"^      - name: (.+)$", assets_job, re.MULTILINE) != [
        "Download all artifacts",
        "List downloaded files",
        "Unpack the Windows package",
        "Build the release assets",
        "Compare the Windows file list with the artifact",
        "List the release assets",
    ]:
        missing.append("package check release assets steps: download, list, unpack, build, compare, list")
    if step_block(assets_job, "Download all artifacts").rstrip("\n") != (
        "name: Download all artifacts\n"
        "        uses: actions/download-artifact@v4\n"
        "        with:\n"
        "          path: artifacts"
    ):
        missing.append("package check release assets download of all artifacts into artifacts")
    if step_block(assets_job, "Build the release assets") != (
        "name: Build the release assets\n"
        "        env:\n"
        "          VERSION: ${{ needs.version.outputs.version }}\n"
        '        run: python3 tools/release/build_release_assets.py --version "$VERSION" --artifacts artifacts --output release-assets\n'
    ):
        missing.append("package check release assets script run with the selected version")
    unpack_step = step_block(assets_job, "Unpack the Windows package")
    compare_step = step_block(assets_job, "Compare the Windows file list with the artifact")
    if any(
        command not in step
        for step, commands in (
            (
                unpack_step,
                (
                    "mkdir artifacts/QEGTRAIN-windows-x64-payload\n",
                    "unzip -q artifacts/QEGTRAIN-windows-x64/QEGTRAIN-windows-x64.zip -d artifacts/QEGTRAIN-windows-x64-payload\n",
                ),
            ),
            (
                compare_step,
                (
                    "unzip -Z1 artifacts/QEGTRAIN-windows-x64/QEGTRAIN-windows-x64.zip | tr '\\\\' '/' | grep -v '/$' | LC_ALL=C sort > \"$RUNNER_TEMP/zip-files.txt\"\n",
                    "jq -r '.assets[\"windows-x64\"].files[]' release-assets/update-manifest.json > \"$RUNNER_TEMP/manifest-files.txt\"\n",
                    'diff "$RUNNER_TEMP/zip-files.txt" "$RUNNER_TEMP/manifest-files.txt"\n',
                ),
            ),
        )
        for command in commands
    ):
        missing.append("package check release assets unpack of the Windows zip and comparison of its file list with the manifest")
    if not assets_job or any(word in assets_job for word in ("upload-artifact", "permissions:", "if:")):
        missing.append("package check release assets job without an upload, a permissions block or a condition")
    if (
        "add_test(NAME test_release_assets\n"
        "\t\tCOMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_release_assets.py)\n" not in cmake
        or not re.search(r"egtrain_label_tests\(unit\b[^)]*\btest_release_assets\b", cmake)
    ):
        missing.append("release assets unit test registered in CTest with the unit label")
    publishing = ("action-gh-release", "gh release", "gh api", "git push", "git commit", "environment:")
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
