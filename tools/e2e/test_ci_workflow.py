#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    workflow = (ROOT / ".github/workflows/cmake.yml").read_text(encoding="utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    gui_smoke = (ROOT / "tools/e2e/gui_autostart_smoke.py").read_text(encoding="utf-8")
    release_workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
    blocks = workflow.split("\n      - ")
    required = {
        "Test": "ctest --test-dir build",
        "Run headless Copenhagen and Brescia smokes": "tools/e2e/headless_smoke.py 3 4",
        "Run editor smoke": "tools/e2e/editor_smoke.sh",
        "Run scene roundtrip smoke": "tools/e2e/roundtrip_smoke.py",
        "Run assignment smoke": "tools/e2e/assignment_smoke.py",
        "Run incident smoke": "tools/e2e/incident_smoke.py",
        "Run scene render smoke": "tools/e2e/scene_render_smoke.sh",
        "Run visual smoke": "tools/e2e/visual_polish_smoke.sh",
        "Configure": "EGTRAIN_ENABLE_SANITIZERS=ON",
    }
    missing = [
        name
        for name, command in required.items()
        if not any(block.startswith(f"name: {name}\n") and command in block for block in blocks)
    ]
    test_steps = [block for block in blocks if block.startswith("name: Test\n")]
    if len(test_steps) != 2 or any(
        "ctest --test-dir build" not in block or "--output-log" not in block
        for block in test_steps
    ):
        missing.append("ctest output logs")
    sanitizer_test = next(
        block
        for block in workflow.split("\n  sanitizer:\n", 1)[1].split("\n      - ")
        if block.startswith("name: Test\n")
    )
    if any(
        option not in sanitizer_test
        for option in (
            "UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1",
            "ASAN_OPTIONS: abort_on_error=1:halt_on_error=1",
        )
    ):
        missing.append("sanitizer failure options")
    if any(
        budget not in sanitizer_test
        for budget in (
            'QEGTRAIN_GUI_SMOKE_MARKER_SECONDS: "360"',
            'QEGTRAIN_GUI_SMOKE_SECONDS: "30"',
        )
    ):
        missing.append("sanitizer GUI smoke time budgets")
    if "set_tests_properties(test_gui_autostart_smoke PROPERTIES TIMEOUT 420)" not in cmake:
        missing.append("GUI smoke CTest timeout")
    if "set_tests_properties(test_lebanon_scene_smoke PROPERTIES TIMEOUT 360)" not in cmake:
        missing.append("Lebanon smoke CTest timeout")
    if "set_tests_properties(test_package_contents_smoke PROPERTIES TIMEOUT 420)" not in cmake:
        missing.append("package smoke CTest timeout")
    if "DEFAULT_HORIZON = 200" not in gui_smoke:
        missing.append("bounded GUI smoke horizon")
    if workflow.count('echo "TMPDIR=$RUNNER_TEMP" >> "$GITHUB_ENV"') != 2:
        missing.append("TMPDIR routing steps for smoke logs")
    uploads = [block for block in blocks if block.startswith("name: Upload failure diagnostics\n")]
    if len(uploads) != 2 or any(
        "if: failure()" not in block
        or "actions/upload-artifact@v4" not in block
        or "runner.temp" not in block
        for block in uploads
    ):
        missing.append("failure artifact steps")
    if not uploads or any(
        path not in uploads[0]
        for path in ("qegtrain-incident-*.log", "/ctest.log", "qegtrain-gui-autostart-smoke.log")
    ):
        missing.append("build job failure logs")
    if len(uploads) < 2 or any(
        path not in uploads[1]
        for path in ("ctest-sanitizer.log", "qegtrain-gui-autostart-smoke.log")
    ):
        missing.append("sanitizer job failure logs")
    application_paths = "\n".join(
        (
            "    paths:",
            "      - '.github/workflows/cmake.yml'",
            "      - 'CMakeLists.txt'",
            "      - 'Info.plist.in'",
            "      - 'EGTRAIN/QEGTRAIN/**'",
            "      - 'tools/**'",
        )
    )
    if workflow.count(application_paths) != 2:
        missing.append("application path filters for pushes and pull requests")
    release_paths = application_paths.replace("cmake.yml", "release.yml").replace(
        "\n      - 'tools/**'", ""
    )
    if release_paths not in release_workflow:
        missing.append("release application path filter")
    macos_package_verification = release_workflow.split(
        "      - name: Verify the presentation package\n", 1
    )[1].split("\n      - ", 1)[0]
    required_macos_validation = (
        "set +e",
        'validation_output="$("$PKG/scene_tool" validate "$PKG/Scenes/Lebanon" 2>&1)"',
        "validation_status=$?",
        "set -e",
        '[ "$validation_status" -eq 1 ]',
        'grep -q scene.services.none <<<"$validation_output"',
        'grep -q scene.trains.none <<<"$validation_output"',
    )
    if (
        any(check not in macos_package_verification for check in required_macos_validation)
        or '"$PKG/scene_tool" validate "$PKG/Scenes/Lebanon" 2>&1 | grep' in macos_package_verification
    ):
        missing.append("macOS scene validation status handling")
    required_macos_package_checks = (
        'APP="$(cd "$PKG/QEGTRAIN.app" && pwd)"',
        'test -d "$APP/Contents/Resources/Input/Input_EGTRAIN_Paimpol"',
        'RUN_DIR="$RUNNER_TEMP/qegtrain-paimpol"',
        'rm -rf "$RUN_DIR"',
        'mkdir -p "$RUN_DIR"',
        'cd "$RUN_DIR"',
        '"$APP/Contents/MacOS/QEGTRAIN" -n 2 -h 300 -g 0 -pax 0 -TSM 0 -RC 0 >"$RUN_DIR/qegtrain.log" 2>&1',
        'run_status=$?',
        'if [ "$run_status" -ne 0 ]; then',
        'cat "$RUN_DIR/qegtrain.log"',
        'grep -q "End of Simulation" "$RUN_DIR/qegtrain.log"',
        'test -f "$RUN_DIR/Output/Output_EGTRAIN_Paimpol/EnergyConsumptionPerTrain.txt"',
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
    if missing:
        raise SystemExit("CI workflows are missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
