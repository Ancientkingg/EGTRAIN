#!/usr/bin/env python3
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUN_DIR = ROOT / "EGTRAIN" / "QEGTRAIN"
DEFAULT_SECONDS = 75
# startup takes under 10s on a local Release build; sanitized Debug on a
# shared CI runner needs a much larger budget before the first sim tick
DEFAULT_MARKER_SECONDS = 120

BAD_PATTERNS = (
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "runtime error",
    "std::length_error",
    "global-buffer-overflow",
    "heap-buffer-overflow",
    "stack-buffer-overflow",
    "Error 1[loadTrainType]",
    "Error 2 [Load_TrainType]",
    "ERROR 3[loadTrainType]",
)
PROGRESS_MARKER = "E2E_GUI_AUTOSTART_RUNNING"


def fail(message: str, log_path: Path, exit_code: int = 1) -> None:
    print(message, file=sys.stderr)
    print(f"log: {log_path}", file=sys.stderr)
    if log_path.exists():
        print("--- log tail ---", file=sys.stderr)
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        for line in lines[-80:]:
            print(line, file=sys.stderr)
    raise SystemExit(exit_code)


def terminate_process_group(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: gui_autostart_smoke.py /path/to/QEGTRAIN [seconds]")

    app = Path(sys.argv[1]).resolve()
    if not app.exists():
        raise SystemExit(f"QEGTRAIN app not found: {app}")

    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else int(os.environ.get("QEGTRAIN_GUI_SMOKE_SECONDS", DEFAULT_SECONDS))
    marker_seconds = int(os.environ.get("QEGTRAIN_GUI_SMOKE_MARKER_SECONDS", DEFAULT_MARKER_SECONDS))
    log_path = Path(tempfile.gettempdir()) / "qegtrain-gui-autostart-smoke.log"

    env = os.environ.copy()
    env["QEGTRAIN_AUTOSTART"] = "1"
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_stack_use_after_return=1")

    args = [
        str(app),
        "-n",
        "3",
        "-h",
        "8000",
        "-g",
        "1",
        "-pax",
        "0",
        "-TSM",
        "0",
        "-RC",
        "0",
    ]

    with log_path.open("wb") as output:
        proc = subprocess.Popen(
            args,
            cwd=RUN_DIR,
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

        # wait for the first simulation tick, then hold a liveness window so
        # crashes during early simulation still fail the run
        marker_deadline = time.monotonic() + marker_seconds
        hold_until = None
        while True:
            rc = proc.poll()
            if rc is not None:
                fail(f"QEGTRAIN exited during the smoke: rc={rc}", log_path)
            if hold_until is None:
                if log_path.exists() and PROGRESS_MARKER in log_path.read_text(encoding="utf-8", errors="replace"):
                    hold_until = time.monotonic() + seconds
                elif time.monotonic() >= marker_deadline:
                    terminate_process_group(proc)
                    fail(f"QEGTRAIN did not report GUI simulation progress within {marker_seconds}s", log_path)
            elif time.monotonic() >= hold_until:
                break
            time.sleep(0.5)

        rc = proc.poll()
        if rc is not None:
            fail(f"QEGTRAIN exited during the {seconds}s liveness window: rc={rc}", log_path)
        terminate_process_group(proc)

    text = log_path.read_text(encoding="utf-8", errors="replace")
    for pattern in BAD_PATTERNS:
        if pattern in text:
            fail(f"QEGTRAIN log contains failure marker: {pattern}", log_path)
    if PROGRESS_MARKER not in text:
        fail("QEGTRAIN did not report GUI simulation progress", log_path)

    print(f"gui autostart smoke passed after {seconds}s of simulation: {log_path}")


if __name__ == "__main__":
    main()
