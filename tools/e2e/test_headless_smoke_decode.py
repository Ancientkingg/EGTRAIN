#!/usr/bin/env python3
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from headless_smoke import (
    NO_ARRIVAL,
    case_command,
    check_no_position_jump,
    check_scene_matches_netherlands,
    check_scene_structure,
    occurrence_errors,
    route_errors,
    run_command,
    scene_output_dir,
)


def timetable_sample(*runs: tuple[str, list[str], list[int], list[int]]) -> str:
    """Return TimetablePoints.txt text: six lines per train, with placeholder positions."""
    return "".join(
        f"{train} IC\n{' '.join(stations)} \n{'0 ' * len(stations)}\n{'0 ' * len(stations)}\n"
        f"{' '.join(map(str, arrivals))} \n{' '.join(map(str, departures))} \n"
        for train, stations, arrivals, departures in runs
    )


def check_position_cases() -> None:
    header = "Time[s]\tSpeed[m/s]\tPosition[m]\tTail_Position[m]\tPower_Cons[kW]\tBX[m]\tinstant_train_energy_consumption[KWh]\tBlock\n"
    forward = [float(i * 10) for i in range(20)]
    reverse = list(reversed(forward))
    cases = [
        ("a", "FWD", forward, "0", None, None),
        ("b", "REV", reverse, "1", None, None),
        ("c", "FWD", forward[:9] + [forward[8] - 2] + forward[10:], "0", None, "against its direction of travel"),
        ("d", "REV", reverse[:9] + [reverse[8] + 2] + reverse[10:], "1", None, "against its direction of travel"),
        ("e", "REV", reverse, "0", None, "against its direction of travel"),
        ("f", "FWD", forward[:9] + [forward[8] - 0.05] + forward[10:], "0", None, None),
        ("g1", "FWD", forward, "other", None, "direction of travel is unknown"),
        ("g2", "FWD", forward, None, None, "direction of travel is unknown"),
        ("g3", "FWD", [], "other", None, None),
        ("h", "FWD", forward[:9] + [forward[8] - 100] + forward[10:], "0", None, "m/s allows"),
        ("i", "FWD", forward[:10] + [forward[9] - 2 + i * 10 for i in range(10)], "0", 10, None),
    ]
    deviations = []
    for letter, train, positions, flag, gap, expected in cases:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            output = scene_output_dir(1, base)
            trajectory = output / "TEMP" / f"Traj_Train_{train}.txt"
            diagram = output / "TrainTrajectories/TrainServicePathDiagram.txt"
            trajectory.parent.mkdir(parents=True)
            diagram.parent.mkdir(parents=True)
            rows = header
            for time, position in enumerate(positions):
                if time == gap:
                    rows += "\n"
                rows += f"{time}\t10\t{position}\t0\t0\t0\t0\tblock\n"
            trajectory.write_bytes(rows.encode("utf-8"))
            if flag is not None:
                name = "OTHER" if flag == "other" else train
                direction = "0" if flag == "other" else flag
                diagram.write_bytes(f"{name}\t1\t{direction}\tA\t0\t10\n".encode("utf-8"))
            try:
                check_no_position_jump(1, base)
            except SystemExit as exc:
                message = str(exc.code)
                if expected is None or expected not in message or trajectory.name not in message:
                    deviations.append(f"{letter}: unexpected failure: {message}")
                elif letter in ("g1", "g2") and str(diagram) not in message:
                    deviations.append(f"{letter}: missing diagram path: {message}")
            except Exception as exc:
                deviations.append(f"{letter}: {type(exc).__name__}: {exc}")
            else:
                if expected is not None:
                    deviations.append(f"{letter}: passed, expected {expected}")
    if deviations:
        raise SystemExit("position cases failed:\n" + "\n".join(deviations))


def main() -> None:
    check_position_cases()
    for case_id in range(1, 5):
        check_scene_structure(case_id)
    check_scene_matches_netherlands(7)

    command = case_command(3)
    expected = ["--scene", str(Path(__file__).resolve().parents[2] / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"),
                "-g", "0", "-TSM", "0", "-RC", "0", "--detailed-trajectories"]
    if command[1:] != expected:
        raise SystemExit(f"headless case command is not the expected one (no GUI, no integrations, detailed trajectories): {command}")

    assignment_output = scene_output_dir(5, Path("/tmp/qegtrain-smoke"))
    if assignment_output.name != "Assignment Gvc-Gdg-Ut":
        raise SystemExit(f"headless output path ignores the canonical scene name: {assignment_output}")

    errors = route_errors("ok\nERROR4 in Route r1\nERROR5 in Route r1\n")
    if errors != ["ERROR4 in Route r1", "ERROR5 in Route r1"]:
        raise SystemExit(f"route error detection failed: {errors}")

    student_output = scene_output_dir(7, Path("/tmp/qegtrain-smoke"))
    if student_output.name != "Amsterdam_Hilversum_Student":
        raise SystemExit(f"headless output path ignores the name of the Amsterdam to Hilversum scene: {student_output}")

    # Four runs 1800 s apart that stop at Asd and end at Hvs.
    stations = ["Asd", "Hvs"]
    runs = [
        ("IC-1", stations, [120, 1500], [180, 1560]),
        ("IC-2", stations, [1920, 3300], [1980, 3360]),
        ("IC-3", stations, [3720, 5100], [3780, 5160]),
        ("IC-4", stations, [5520, 6900], [5580, 6960]),
    ]
    errors = occurrence_errors(timetable_sample(*runs), 8000, "IC", 4)
    if errors:
        raise SystemExit(f"complete occurrences reported errors: {errors}")
    # One run is replaced or left out at a time. Exactly one error must name that run and the cause.
    changes = [
        (2, None, "IC-3", "is missing"),
        (2, ("IC-3", ["Hvs", "Asd"], [5100, 5400], [5160, 5460]), "IC-3", "does not end at Hvs"),
        (1, ("IC-2", stations, [1920, 1400], [1980, 1460]), "IC-2", "not after the arrival"),
        (1, ("IC-2", stations, [1920, 1500], [1980, 1560]), "IC-2", "not after the arrival"),
        (0, ("IC-1", stations, [120, NO_ARRIVAL], [180, NO_ARRIVAL]), "IC-1", "not above 0"),
        (0, ("IC-1", stations, [120, 0], [180, 60]), "IC-1", "not above 0"),
        (1, ("IC-2", stations, [1920], [1980, 3360]), "IC-2", "fewer arrival or departure times"),
        (1, ("IC-2", stations, [1920, 3300], [1980]), "IC-2", "fewer arrival or departure times"),
        (1, ("IC-2", stations, [1920, 3300], [1980, 3200]), "IC-2", "departs from Hvs"),
        (3, ("IC-4", stations, [5520, 6900], [5580, 8000]), "IC-4", "departs from Hvs"),
    ]
    for position, run, train, cause in changes:
        replaced = runs[:position] + ([run] if run else []) + runs[position + 1 :]
        errors = occurrence_errors(timetable_sample(*replaced), 8000, "IC", 4)
        if len(errors) != 1 or not errors[0].startswith(train) or cause not in errors[0]:
            raise SystemExit(f"{train} ({cause}) was not reported as the only error: {errors}")

    proc = run_command(
        [
            sys.executable,
            "-c",
            "import sys; sys.stdout.buffer.write(bytes([0xfc]))",
        ]
    )
    if proc.returncode != 0:
        raise SystemExit(f"expected clean subprocess exit, got {proc.returncode}")
    if "\ufffd" not in proc.stdout:
        raise SystemExit("expected replacement character for undecodable stdout")


if __name__ == "__main__":
    main()
