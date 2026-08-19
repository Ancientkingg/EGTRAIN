#!/usr/bin/env python3
import hashlib
import json
import math
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
RUN_DIR = ROOT / "EGTRAIN/QEGTRAIN"
SCENE_DIR = RUN_DIR / "Scenes"

SCENES = {
    1: "Netherlands",
    2: "Paimpol",
    3: "Copenhagen",
    4: "Milano_Brescia",
    5: "Assignment_Gvc_Gdg_Ut",
    6: "Lebanon",
}

ASSERT_MOVEMENT = {1, 2, 3, 4, 5}
ASSERT_STATION_ARRIVALS = {1, 2, 3, 4, 5}

# d3f5c7005c7030ba3745c8a41b0572e61974bd15 is the last pre-cutover
# runtime baseline. These checks keep one representative observable per
# original case instead of retaining large generated output trees.
ORIGINAL_CASE_PARITY = {
    1: {
        "structure": (1984, 708, 41, 156, 74),
        "station_ids": "7eb1725701ff3d105f0ff84ba9ac23cd27fe5642f462947d9fa7bf28492305ff",
        "route_ids": "dea4515fec5d4b1cf241b7276b38dce3d283474635db5b379d6582e21f10e3c9",
        "trains": 40,
        "representative": ("SPR_2-1", 100, 18940.6, 7999, 1252.38, 0, 100),
        "stops": ("425d9c987fddb82cf8812520e9094efd165533d5265aa4dd59367fb9ea146f28", 132, 847, 10, 350),
    },
    2: {
        "structure": (140, 8, 10, 15, 15),
        "station_ids": "76ee593e12c0ed1fc4ead4d497d16dc3f658e8a13e4fd60d233f2d8b4d7c05b5",
        "route_ids": "b05b7d25ec4a281c582032a3b86c6cfef9c4cdd344be4da1340856531086b7ca",
        "trains": 2,
        "representative": ("Guin-Paim-EXPRESS-1-1", 10, 0.595063, 2571, 37081.3, 250, 10),
        "stops": ("9d358eb8bb2575c8111f98791e0f1bca7b84381fafba1197173f684e7a998331", 43, 2558, 10, 150),
    },
    3: {
        "structure": (1513, 328, 94, 203, 24),
        "station_ids": "4d0343d22fd0d42f1ca19d406fc379a19589d5945cad35d9b7a76fdd7b927f78",
        "route_ids": "39b7ca1c397a4830fe27ab3fe14366ba0b842b857ed3a4c819e24bc2c68565a7",
        "trains": 196,
        "representative": ("F-Hellerup-NyEllebjerg-2-1", 340, 50037.4, 934, 37943.0, 950, 10),
        "stops": ("b11fe2560aa043b373523dcab19b1bf291f6a2a4c95f877141a26fb073c6fd34", 367, 927, 10, 900),
    },
    4: {
        "structure": (312, 109, 29, 92, 48),
        "station_ids": "091b2621fa7695b9fb368e13caa966d0da1864048cbae9fa91ea37f6aff973ca",
        "route_ids": "73c9e9fd30a7f2c82ad5f7fe202083c6c3aa87c0a36215ace7d5a393da189494",
        "trains": 65,
        "representative": ("201-1", 1738, 9636.22, 3673, 78768.2, 0, 0.1),
        "stops": ("b76878b055e51183a425ccdfe008049e66b8980fcc23f62d46636a0a53cd8981", 1855, 3666, 1, 1),
    },
}


def run_command(args, cwd=None, input=None, timeout=None, env=None):
    input_bytes = input.encode("utf-8") if isinstance(input, str) else input
    proc = subprocess.run(
        args,
        cwd=cwd,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        env=env,
    )
    return subprocess.CompletedProcess(
        proc.args,
        proc.returncode,
        proc.stdout.decode("utf-8", errors="replace"),
        proc.stderr,
    )


def case_command(case_id: int) -> list[str]:
    try:
        scene_name = SCENES[case_id]
    except KeyError as exc:
        raise ValueError(f"unknown canonical case id: {case_id}") from exc
    return [str(APP), "--scene", str(SCENE_DIR / scene_name), "-g", "0", "-TSM", "0", "-RC", "0"]


def scene_output_dir(case_id: int, out_base: Path = RUN_DIR) -> Path:
    scene = json.loads((SCENE_DIR / SCENES[case_id] / "scene.json").read_text(encoding="utf-8"))
    return out_base / "Output" / scene["name"]


def _digest(ids: list[str]) -> str:
    return hashlib.sha256("\n".join(ids).encode("utf-8")).hexdigest()


def check_scene_structure(case_id: int) -> None:
    expected = ORIGINAL_CASE_PARITY[case_id]
    scene_dir = SCENE_DIR / SCENES[case_id]
    infrastructure = json.loads((scene_dir / "infrastructure.json").read_text(encoding="utf-8"))
    stations = json.loads((scene_dir / "stations.json").read_text(encoding="utf-8"))
    signalling = json.loads((scene_dir / "signalling.json").read_text(encoding="utf-8"))
    station_rows = stations["stations"]
    actual = (
        len(infrastructure["blocks"]),
        len(infrastructure["connections"]),
        len(station_rows),
        sum(len(station["platforms"]) for station in station_rows),
        len(signalling["routes"]),
    )
    if actual != expected["structure"]:
        raise SystemExit(f"case {case_id} structure changed: expected {expected['structure']}, got {actual}")
    if _digest(sorted(station["id"] for station in station_rows)) != expected["station_ids"]:
        raise SystemExit(f"case {case_id} station identifiers changed")
    if _digest(sorted(route["id"] for route in signalling["routes"])) != expected["route_ids"]:
        raise SystemExit(f"case {case_id} route identifiers changed")
    print(f"PASS case {case_id} matches the original-case structure baseline")


def route_errors(output: str) -> list[str]:
    return [line for line in output.splitlines() if line.startswith(("ERROR4 in Route", "ERROR5 in Route"))]


def run_case(case_id: int, cwd: Path = RUN_DIR, out_base: Path = RUN_DIR) -> None:
    log = ROOT / f"tools/e2e/headless_case_{case_id}.log"
    try:
        env = os.environ.copy()
        env["QEGTRAIN_OUTPUT_DIR"] = str(out_base)
        proc = run_command(
            case_command(case_id),
            cwd=cwd,
            # The largest case (Copenhagen, 185 trains x 8000 steps) runs for about
            # five minutes on a fast machine, and slow CI runners have hit the
            # previous 600 s ceiling, so keep a wide margin over the worst case.
            timeout=900,
            env=env,
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
    out_dir = scene_output_dir(case_id, out_base) / "TrainTrajectories"
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
    stats = scene_output_dir(case_id, out_base) / "TrainTrajectories/Stats_Stations.txt"
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


def check_original_case_runtime(case_id: int, out_base: Path = RUN_DIR) -> None:
    expected = ORIGINAL_CASE_PARITY[case_id]
    output_dir = scene_output_dir(case_id, out_base)

    energy_rows = (output_dir / "EnergyConsumptionPerTrain.txt").read_text(
        encoding="utf-8", errors="replace"
    ).splitlines()[1:]
    train_count = sum(bool(row.strip()) for row in energy_rows)
    if train_count != expected["trains"]:
        raise SystemExit(f"case {case_id} expanded {train_count} trains; expected {expected['trains']}")

    train, start_time, start_position, end_time, end_position, time_tolerance, position_tolerance = expected[
        "representative"
    ]
    trajectory = output_dir / "TrainTrajectories/TrainServicePathDiagram.txt"
    samples = []
    for line in trajectory.read_text(encoding="utf-8", errors="replace").splitlines():
        cells = line.split("\t")
        if cells[0] != train:
            continue
        for time_seconds, cell in enumerate(cells[4:]):
            if cell:
                samples.append((time_seconds, float(cell)))
        break
    if not samples:
        raise SystemExit(f"case {case_id} has no trajectory for baseline train {train}")
    if samples[0][0] != start_time or abs(samples[0][1] - start_position) > 0.1:
        raise SystemExit(f"case {case_id} baseline train {train} changed its trajectory origin")
    if abs(samples[-1][0] - end_time) > time_tolerance or abs(samples[-1][1] - end_position) > position_tolerance:
        raise SystemExit(
            f"case {case_id} baseline train {train} ended at {samples[-1]}; "
            f"expected ({end_time}, {end_position}) within ({time_tolerance}s, {position_tolerance}m)"
        )

    stop_digest, first_arrival, last_arrival, first_tolerance, last_tolerance = expected["stops"]
    timetable = output_dir / "TrainTrajectories/TimetablePoints.txt"
    lines = timetable.read_text(encoding="utf-8", errors="replace").splitlines()
    for index, line in enumerate(lines):
        if line.split()[:1] == [train]:
            station_names = lines[index + 1].split()
            arrivals = [float(value) for value in lines[index + 4].split()]
            break
    else:
        raise SystemExit(f"case {case_id} has no timetable for baseline train {train}")
    if _digest(station_names) != stop_digest:
        raise SystemExit(f"case {case_id} baseline train {train} changed its served-station sequence")
    if abs(arrivals[0] - first_arrival) > first_tolerance or abs(arrivals[-1] - last_arrival) > last_tolerance:
        raise SystemExit(
            f"case {case_id} baseline train {train} arrivals changed: "
            f"expected first/last {first_arrival}/{last_arrival}, got {arrivals[0]}/{arrivals[-1]}"
        )
    print(f"PASS case {case_id} matches the pre-cutover runtime observables ({train_count} trains)")


def main() -> None:
    selected = [int(arg) for arg in sys.argv[1:]] or [1, 2, 3, 4, 5, 6]
    for case_id in selected:
        if case_id in ORIGINAL_CASE_PARITY:
            check_scene_structure(case_id)
        run_case(case_id)
        if case_id in ASSERT_MOVEMENT:
            check_movement(case_id)
        if case_id in ORIGINAL_CASE_PARITY:
            check_original_case_runtime(case_id)
    for case_id in selected:
        if case_id in ASSERT_STATION_ARRIVALS:
            check_station_arrivals(case_id)


if __name__ == "__main__":
    main()
