#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    workflow = (ROOT / ".github/workflows/cmake.yml").read_text(encoding="utf-8")
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
    if missing:
        raise SystemExit("CI workflows are missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
