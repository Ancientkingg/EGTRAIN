#!/usr/bin/env python3
"""Run every committed scene headless in a build with sanitizers and keep the full log of each run.

usage: leak_check_smoke.py APP LOG_DIR

APP is a QEGTRAIN built with EGTRAIN_ENABLE_SANITIZERS=ON, and ASAN_OPTIONS must contain
detect_leaks=1, so that LeakSanitizer checks the heap when the process exits. Each scene directory
below EGTRAIN/QEGTRAIN/Scenes runs once with a short horizon and --detailed-trajectories. The output
of a run, with the sanitizer report in it, goes to LOG_DIR/SCENE.log.

A run fails when its log holds a LeakSanitizer or AddressSanitizer report (a leak or a double free
among them), when the process exits with a code other than 0, or when it prints no 'End of
Simulation'. All scenes run before the script exits, so one start lists every scene that fails.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENES = ROOT / "EGTRAIN/QEGTRAIN/Scenes"
HORIZON_SECONDS = "600"
RUN_TIMEOUT_SECONDS = 1200
REPORT_MARKERS = ("ERROR: LeakSanitizer", "ERROR: AddressSanitizer")
FAILURE_TAIL_CHARS = 3000


def run_scene(app: Path, scene: Path, log: Path) -> str:
    """Run the scene into a temporary output folder, write its output to log and return why it failed, or ''."""
    command = [str(app), "--scene", str(scene), "-h", HORIZON_SECONDS, "-g", "0",
               "--detailed-trajectories"]
    with tempfile.TemporaryDirectory(prefix="qegtrain-leak-") as output:
        env = os.environ.copy()
        env["QEGTRAIN_OUTPUT_DIR"] = output
        env["QT_QPA_PLATFORM"] = "offscreen"
        try:
            proc = subprocess.run(command, cwd=app.parent, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            log.write_bytes(exc.stdout or b"")
            return f"timed out after {RUN_TIMEOUT_SECONDS}s"
    text = proc.stdout.decode("utf-8", errors="replace")
    log.write_text(text, encoding="utf-8")
    if any(marker in text for marker in REPORT_MARKERS):
        return f"the sanitizer reported an error, exit code {proc.returncode}"
    if proc.returncode != 0:
        return f"exited with {proc.returncode}"
    if "End of Simulation" not in text:
        return "printed no 'End of Simulation'"
    return ""


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    app = Path(sys.argv[1]).resolve()
    log_dir = Path(sys.argv[2]).resolve()
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")
    if "detect_leaks=1" not in os.environ.get("ASAN_OPTIONS", ""):
        raise SystemExit("ASAN_OPTIONS must contain detect_leaks=1, or the runs check no leaks")
    scenes = sorted(path for path in SCENES.iterdir() if (path / "scene.json").is_file())
    if not scenes:
        raise SystemExit(f"no scene found in {SCENES}")
    log_dir.mkdir(parents=True, exist_ok=True)

    failed = []
    for scene in scenes:
        log = log_dir / f"{scene.name}.log"
        reason = run_scene(app, scene, log)
        if not reason:
            print(f"PASS {scene.name}: no sanitizer report, log {log}", flush=True)
            continue
        failed.append(scene.name)
        print(f"FAIL {scene.name}: {reason}, full log {log}", flush=True)
        print(log.read_text(encoding="utf-8", errors="replace")[-FAILURE_TAIL_CHARS:], flush=True)
    if failed:
        raise SystemExit(f"{len(failed)} of {len(scenes)} scenes failed: {', '.join(failed)}")
    print(f"leak check passed: {len(scenes)} scenes")


if __name__ == "__main__":
    main()
