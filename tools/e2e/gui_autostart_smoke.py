#!/usr/bin/env python3
import math
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUN_DIR = ROOT / "EGTRAIN" / "QEGTRAIN"
DEFAULT_SECONDS = 75
# Keep the smoke short enough to finish on hosted runners while still checking
# completed-run results and generated energy output.
DEFAULT_HORIZON = 200
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
RESULTS_MARKER = "E2E_GUI_RUN_RESULTS"


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
    horizon = os.environ.get("QEGTRAIN_GUI_SMOKE_HORIZON", str(DEFAULT_HORIZON))
    log_path = Path(tempfile.gettempdir()) / "qegtrain-gui-autostart-smoke.log"

    env = os.environ.copy()
    env["QEGTRAIN_AUTOSTART"] = "1"
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_stack_use_after_return=1")
    run_started = time.time()

    args = [
        str(app),
        "-n",
        "3",
        "-h",
        horizon,
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

        # wait for the first simulation tick and completed-run results marker
        # so crashes or incomplete result wiring fail the run
        marker_deadline = time.monotonic() + marker_seconds
        while True:
            rc = proc.poll()
            if rc is not None:
                fail(f"QEGTRAIN exited during the smoke: rc={rc}", log_path)
            text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            if RESULTS_MARKER in text:
                break
            if time.monotonic() >= marker_deadline:
                terminate_process_group(proc)
                missing = RESULTS_MARKER if PROGRESS_MARKER in text else PROGRESS_MARKER
                fail(f"QEGTRAIN did not report {missing} within {marker_seconds}s", log_path)
            time.sleep(0.5)

        hold_until = time.monotonic() + seconds
        while time.monotonic() < hold_until:
            rc = proc.poll()
            if rc is not None:
                fail(f"QEGTRAIN exited during the {seconds}s liveness window: rc={rc}", log_path)
            time.sleep(0.5)
        terminate_process_group(proc)

    text = log_path.read_text(encoding="utf-8", errors="replace")
    for pattern in BAD_PATTERNS:
        if pattern in text:
            fail(f"QEGTRAIN log contains failure marker: {pattern}", log_path)
    if PROGRESS_MARKER not in text:
        fail("QEGTRAIN did not report GUI simulation progress", log_path)

    result_match = re.search(
        rf"^{re.escape(RESULTS_MARKER)} rows=(\d+) trains=(\d+) dock_visible=(\d+) output_dir=(.*)$",
        text,
        re.MULTILINE,
    )
    if not result_match:
        fail("QEGTRAIN did not report a valid run-results marker", log_path)
    rows, trains, dock_visible = (int(result_match.group(i)) for i in range(1, 4))
    output_dir = Path(result_match.group(4).strip())
    if rows != trains + 1:
        fail(f"run-results row count mismatch: rows={rows}, trains={trains}", log_path)
    if dock_visible != 1:
        fail("run-results dock is not visible", log_path)

    per_train_path = output_dir / "EnergyConsumptionPerTrain.txt"
    total_path = output_dir / "TotalEnergyConsumption.txt"
    if not per_train_path.is_file() or not total_path.is_file():
        fail(f"missing energy output files in {output_dir}", log_path)
    if per_train_path.stat().st_mtime < run_started or total_path.stat().st_mtime < run_started:
        fail("energy output files were not produced by this run", log_path)

    per_train_lines = per_train_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if len(per_train_lines) < 2 or len(per_train_lines) - 1 != trains:
        fail(f"energy train-row count mismatch in {per_train_path}", log_path)
    energy_values = []
    for line in per_train_lines[1:]:
        fields = line.split()
        if len(fields) != 5:
            fail(f"malformed energy train row: {line}", log_path)
        try:
            energy_values.extend(float(value) for value in fields[1:])
        except ValueError:
            fail(f"non-numeric energy train row: {line}", log_path)

    total_lines = total_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if len(total_lines) < 2:
        fail(f"missing total energy row in {total_path}", log_path)
    try:
        total_values = [float(value) for value in total_lines[1].split()]
    except ValueError:
        fail(f"non-numeric total energy row: {total_lines[1]}", log_path)
    energy_values.extend(total_values)
    if not energy_values or not all(math.isfinite(value) for value in energy_values):
        fail("energy outputs contain non-finite values", log_path)
    if not any(value != 0.0 for value in energy_values):
        fail("energy outputs contain no nonzero computed value", log_path)

    print(f"gui autostart smoke passed after {seconds}s of simulation: {log_path}")


if __name__ == "__main__":
    main()
