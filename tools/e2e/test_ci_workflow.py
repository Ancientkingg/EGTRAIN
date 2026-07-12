#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    workflow = (ROOT / ".github/workflows/cmake.yml").read_text(encoding="utf-8")
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
    if missing:
        raise SystemExit("CMake workflow is missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
