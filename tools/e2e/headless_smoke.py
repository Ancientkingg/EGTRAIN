#!/usr/bin/env python3
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
RUN_DIR = ROOT / "EGTRAIN/QEGTRAIN"
PROMPTS = "n\n0\n0\n0\n0\n"

CASES = {
    3: "Output/Output_EGTRAIN_Banedanmark/TrainTrajectories",
    4: "Output/Output_EGTRAIN_Milano_Brescia/TrainTrajectories",
}


def run_case(case_id: int) -> None:
    proc = subprocess.run(
        [str(APP), "-n", str(case_id)],
        cwd=RUN_DIR,
        input=PROMPTS,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
    )
    log = ROOT / f"tools/e2e/headless_case_{case_id}.log"
    log.write_text(proc.stdout, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"case {case_id} failed with {proc.returncode}; see {log}")
    print(f"PASS case {case_id} exited cleanly")


_TOL = 1e-9


def _is_valid_sample(value: float) -> bool:
    return math.isfinite(value) and value != -1.0 and value > -9990


def check_trajectory_file(path: Path) -> tuple[bool, bool]:
    has_changing = False
    has_valid = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines()[1:]:
        values = []
        for token in line.split()[1:]:
            try:
                value = float(token)
            except ValueError:
                continue
            if _is_valid_sample(value):
                values.append(value)
        if not has_changing and len(values) >= 2 and (max(values) - min(values)) > _TOL:
            has_changing = True
        if not has_valid:
            for v in values:
                if abs(v) > _TOL:
                    has_valid = True
                    break
        if has_changing and has_valid:
            break
    return has_changing, has_valid


def check_movement(case_id: int) -> None:
    out_dir = RUN_DIR / CASES[case_id]
    candidates = [
        out_dir / "TrainPathDiagram.txt",
        out_dir / "TrainServicePathDiagram.txt",
        out_dir / "TrainServicePathDiagramToTestRunTime.txt",
        out_dir.parent / "TrainTrajectoriesfull/TrainPathDiagram.txt",
    ]

    found_changing = False
    found_valid = False
    checked_files = []

    for path in candidates:
        if path.exists():
            checked_files.append(path.name)
            changing, valid = check_trajectory_file(path)
            found_changing = found_changing or changing
            found_valid = found_valid or valid

    if not found_changing:
        raise SystemExit(f"case {case_id} failed: no changing trajectory positions in {checked_files or 'no files found'}")
    print(f"PASS case {case_id} has changing trajectory positions")

    if not found_valid:
        raise SystemExit(f"case {case_id} failed: no valid non-zero/non-sentinel trajectory samples in {checked_files or 'no files found'}")
    print(f"PASS case {case_id} has non-zero/non-sentinel trajectory samples")


def check_station_arrivals(case_id: int = 3) -> None:
    stats = RUN_DIR / CASES[case_id] / "Stats_Stations.txt"
    rows = 0
    for line in stats.read_text(encoding="utf-8", errors="replace").splitlines()[1:]:
        parts = line.split()
        if not parts or parts[0] in {"DwT_Dist", "TOTALS", "Final_Station"}:
            continue
        if parts[0].startswith("Ent_") or len(parts) < 11:
            continue
        try:
            stopped = int(float(parts[10]))
            max_delay = float(parts[4])
        except ValueError:
            continue
        if stopped > 0 and max_delay >= 0:
            rows += 1
    if rows == 0:
        raise SystemExit(f"case {case_id} has no real station rows with served arrivals")
    print(f"PASS case {case_id} has {rows} served station rows")


def main() -> None:
    selected = [int(arg) for arg in sys.argv[1:]] or [3, 4]
    for case_id in selected:
        run_case(case_id)
        check_movement(case_id)
    for case_id in selected:
        check_station_arrivals(case_id)


if __name__ == "__main__":
    main()
