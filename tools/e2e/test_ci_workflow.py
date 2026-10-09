#!/usr/bin/env python3
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    workflow = (ROOT / ".github/workflows/cmake.yml").read_text(encoding="utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    gui_smoke = (ROOT / "tools/e2e/gui_autostart_smoke.py").read_text(encoding="utf-8")
    release_workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
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
    if "  push:\n    branches: [main]\n" not in validation_trigger:
        missing.append("main validation trigger")
    if "  pull_request:\n    branches: [main]\n" not in validation_trigger:
        missing.append("main pull request validation trigger")
    if "paths:" in validation_trigger:
        missing.append("main validation must not use a code allowlist")
    release_trigger = release_workflow.split("\njobs:", 1)[0]
    if not re.search(r"push:\n\s+branches:\n\s+- production\n", release_trigger):
        missing.append("production release trigger")
    if not re.search(r"pull_request:\n\s+branches:\n\s+- production\n", release_trigger):
        missing.append("production pull request validation trigger")
    if "paths:" in release_trigger:
        missing.append("production release must not use a code allowlist")
    expected_ignored_paths = {"**.md", "docs/**", "LICENSE", ".github/ISSUE_TEMPLATE/**"}
    for name, trigger in (("main", validation_trigger), ("production", release_trigger)):
        for event in ("push", "pull_request"):
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
            ('VERSION="$(tr -d \'\\r\\n\' < build/EGTRAIN_VERSION)"', release_workflow),
            ('project(EGTRAIN VERSION ${EGTRAIN_VERSION} LANGUAGES', cmake),
            ('version="${{ needs.version.outputs.version }}"', release_workflow),
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
    publish_job = release_workflow.split("\n  release:\n", 1)[1]
    publish_condition = publish_job.split("\n    runs-on:", 1)[0]
    if "needs: [version, validation, sanitizer, package-macos, package-windows, package-linux]" not in publish_condition:
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
    if release_workflow.count('"-DEGTRAIN_VERSION=${{ needs.version.outputs.version }}"') != 5:
        missing.append("quoted shared release version in all five build jobs")
    if release_workflow.count("    needs: version\n") != 5:
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
    )
    if any(f"            {name}\n" not in release_workflow for name in scene_names) or any(
        command not in release_workflow
        for command in (
            'build/scene_tool pack "EGTRAIN/QEGTRAIN/Scenes/$name" "$bundle"',
            'build/scene_tool validate "$bundle"',
            "name: EGTRAIN-scenes",
        )
    ):
        missing.append("six deterministic release scene bundles")
    if any(
        f"artifacts/EGTRAIN-scenes/{name}.egscene" not in release_workflow
        for name in scene_names
    ) or "artifacts/EGTRAIN-scenes/*.egscene" not in release_workflow:
        missing.append("scene bundles in published release assets")
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
            'if [[ "$count" != "10" ]]',
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
        'windeployqt.exe" --release --no-translations --compiler-runtime "$dist/QEGTRAIN.exe"',
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
    if missing:
        raise SystemExit("CI workflows are missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
