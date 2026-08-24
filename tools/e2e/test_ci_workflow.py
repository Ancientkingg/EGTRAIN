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
        missing.append("unfiltered main validation triggers")
    release_trigger = release_workflow.split("\njobs:", 1)[0]
    if not re.search(r"push:\n\s+branches:\n\s+- production\n", release_trigger):
        missing.append("production release trigger")
    if not re.search(r"pull_request:\n\s+branches:\n\s+- production\n", release_trigger):
        missing.append("production pull request validation trigger")
    if "paths:" in release_trigger:
        missing.append("unfiltered production release trigger")
    if "      - main\n" in release_trigger or "ci/release-pipeline" in release_trigger:
        missing.append("stale non-production release trigger")
    if "      - 'v*'" not in release_trigger:
        missing.append("version tag release trigger")
    if "  workflow_dispatch:\n" not in release_trigger:
        missing.append("manual release trigger")
    if not re.search(r"project\(EGTRAIN VERSION \d+\.\d+\.\d+ LANGUAGES", cmake):
        missing.append("three-component CMake application version")
    if any(
        marker not in content
        for marker, content in (
            ('add_compile_definitions(EGTRAIN_APP_VERSION=\\"${PROJECT_VERSION}\\")', cmake),
            ('file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/EGTRAIN_VERSION"', cmake),
            ("setApplicationVersion(QStringLiteral(EGTRAIN_APP_VERSION))", main_cpp),
            ('VALUE "ProductVersion", "@PROJECT_VERSION@\\0"', windows_resource),
            ('VERSION="$(tr -d \'\\r\\n\' < build/EGTRAIN_VERSION)"', release_workflow),
            ('if [[ "${tag#v}" != "$cmake_version" ]]', release_workflow),
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
    if "needs: [validation, sanitizer, package-macos, package-windows, package-linux]" not in publish_condition:
        missing.append("release publication validation gates")
    if "refs/heads/production" not in publish_condition or "refs/heads/main" in publish_condition:
        missing.append("production release publish condition")
    if 'tag="main-' in release_workflow or 'name="EGTRAIN main build' in release_workflow:
        missing.append("stale main release metadata")
    if 'tag="production-' not in release_workflow or 'name="EGTRAIN production build' not in release_workflow:
        missing.append("production release metadata")
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
    if missing:
        raise SystemExit("CI workflows are missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
