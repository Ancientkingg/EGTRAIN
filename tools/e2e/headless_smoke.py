#!/usr/bin/env python3
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
RUN_DIR = ROOT / "EGTRAIN/QEGTRAIN"

CASES = {
    1: "Output/Output_EGTRAIN_Netherlands/TrainTrajectories",
    2: "Output/Output_EGTRAIN_Paimpol/TrainTrajectories",
    3: "Output/Output_EGTRAIN_Banedanmark/TrainTrajectories",
    4: "Output/Output_EGTRAIN_Milano_Brescia/TrainTrajectories",
    5: "Output/Output_EGTRAIN_Assignment/TrainTrajectories",
}
ASSERT_MOVEMENT = {2, 3, 4}
ASSERT_STATION_ARRIVALS = {2, 3, 4}


def run_command(args, cwd=None, input=None, timeout=None):
    input_bytes = input.encode("utf-8") if isinstance(input, str) else input
    proc = subprocess.run(
        args,
        cwd=cwd,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )
    return subprocess.CompletedProcess(
        proc.args,
        proc.returncode,
        proc.stdout.decode("utf-8", errors="replace"),
        proc.stderr,
    )


def case_command(case_id: int) -> list[str]:
    return [str(APP), "-n", str(case_id), "-g", "0", "-TSM", "0", "-RC", "0"]


def route_errors(output: str) -> list[str]:
    return [line for line in output.splitlines() if line.startswith(("ERROR4 in Route", "ERROR5 in Route"))]


def run_case(case_id: int, cwd: Path = RUN_DIR, out_base: Path = RUN_DIR) -> None:
    log = ROOT / f"tools/e2e/headless_case_{case_id}.log"
    try:
        proc = run_command(
            case_command(case_id),
            cwd=cwd,
            # The largest case (Copenhagen, 185 trains x 8000 steps) runs for about
            # five minutes on a fast machine, and slow CI runners have hit the
            # previous 600 s ceiling, so keep a wide margin over the worst case.
            timeout=900,
        )
    except subprocess.TimeoutExpired as err:
        partial = err.stdout or b""
        if isinstance(partial, bytes):
            partial = partial.decode("utf-8", errors="replace")
        log.write_text(partial, encoding="utf-8", errors="replace")
        raise SystemExit(f"case {case_id} timed out after 900s; see {log}")
    log.write_text(proc.stdout, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"case {case_id} failed with {proc.returncode}; see {log}")
    if case_id == 3 and (errors := route_errors(proc.stdout)):
        raise SystemExit(f"case 3 reported {len(errors)} broken route transitions; see {log}")
    print(f"PASS case {case_id} exited cleanly")


_TOL = 1e-9


def _is_valid_sample(value: float) -> bool:
    return math.isfinite(value) and value != -1.0 and value > -9990


def _position_cells(line: str) -> list[str]:
    """Return only the position columns of a trajectory row.

    TrainServicePathDiagram.txt is tab separated and starts each row with four
    metadata cells: trainDescription, dispLineID, reversed_direction, corridor.
    Splitting on whitespace lets reversed_direction (often 1) leak in as a fake
    non-zero, changing position, so parse tab rows explicitly and drop the four
    leading cells. Space separated diagrams (TrainPathDiagram.txt) carry a
    header row and a single train-name label per row instead.
    """
    if "\t" in line:
        return line.split("\t")[4:]
    parts = line.split()
    if not parts or parts[0].startswith("Train/Time"):
        return []
    return parts[1:]


def check_trajectory_file(path: Path) -> tuple[bool, bool]:
    has_changing = False
    has_valid = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        values = []
        for token in _position_cells(line):
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


def check_movement(case_id: int, out_base: Path = RUN_DIR) -> None:
    out_dir = out_base / CASES[case_id]
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


def check_station_arrivals(case_id: int = 3, out_base: Path = RUN_DIR) -> None:
    stats = out_base / CASES[case_id] / "Stats_Stations.txt"
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
    selected = [int(arg) for arg in sys.argv[1:]] or [1, 2, 3, 4]
    for case_id in selected:
        run_case(case_id)
        if case_id in ASSERT_MOVEMENT:
            check_movement(case_id)
    for case_id in selected:
        if case_id in ASSERT_STATION_ARRIVALS:
            check_station_arrivals(case_id)


if __name__ == "__main__":
    main()
