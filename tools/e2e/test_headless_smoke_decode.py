#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import headless_smoke
from headless_smoke import (
    NO_ARRIVAL,
    blocking_time_records,
    build_executable_paths,
    case_command,
    check_blocking_times,
    check_no_position_jump,
    check_scene_matches_netherlands,
    check_scene_structure,
    find_executable,
    occurrence_errors,
    route_errors,
    run_command,
    scene_output_dir,
    services_not_ended,
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


def check_executable_lookup() -> None:
    with tempfile.TemporaryDirectory() as tmp, mock.patch.object(headless_smoke, "ROOT", Path(tmp)):
        root = Path(tmp)
        program = root / "program"
        program.write_text("", encoding="utf-8")
        with mock.patch.dict(os.environ, {"QEGTRAIN_APP": str(program)}):
            if find_executable("QEGTRAIN", "QEGTRAIN_APP") != program:
                raise SystemExit("QEGTRAIN_APP is not used for the application")
        before = os.getcwd()
        os.chdir(tmp)
        try:
            with mock.patch.dict(os.environ, {"QEGTRAIN_APP": "program"}):
                if find_executable("QEGTRAIN", "QEGTRAIN_APP") != Path(os.getcwd()) / "program":
                    raise SystemExit("a relative QEGTRAIN_APP is not made absolute")
        finally:
            os.chdir(before)

        expected = {
            "darwin": {"QEGTRAIN": ["build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"], "scene_tool": ["build/scene_tool"]},
            "linux": {"QEGTRAIN": ["build/QEGTRAIN"], "scene_tool": ["build/scene_tool"]},
            "win32": {"QEGTRAIN": ["build/QEGTRAIN.exe", "build/Release/QEGTRAIN.exe"]},
        }
        for platform, programs in expected.items():
            with mock.patch.object(sys, "platform", platform):
                for name, relative in programs.items():
                    if build_executable_paths(name) != [root / path for path in relative]:
                        raise SystemExit(f"{platform} looks for the wrong paths for {name}: {build_executable_paths(name)}")

        for variable, override, wanted in (
            ("QEGTRAIN_APP", str(root / "missing"), str(root / "missing")),
            ("QEGTRAIN_APP", "", str(root / "build/QEGTRAIN")),
        ):
            with mock.patch.dict(os.environ, {variable: override}), mock.patch.object(sys, "platform", "linux"):
                try:
                    find_executable("QEGTRAIN", variable)
                except SystemExit as err:
                    if wanted not in str(err):
                        raise SystemExit(f"the error for a missing program does not name {wanted}: {err}")
                else:
                    raise SystemExit("a missing program was accepted")

        (root / "build").mkdir()
        (root / "build/QEGTRAIN").write_text("", encoding="utf-8")
        with mock.patch.dict(os.environ, {"QEGTRAIN_APP": ""}), mock.patch.object(sys, "platform", "linux"):
            if find_executable("QEGTRAIN", "QEGTRAIN_APP") != root / "build/QEGTRAIN":
                raise SystemExit("the Linux build is not found without QEGTRAIN_APP")


def check_case_logs() -> None:
    """A case writes its log into the folder that QEGTRAIN_SMOKE_LOG_DIR names, which it creates, and names the log on a
    failure. Without the variable the log goes to tools/e2e."""
    with tempfile.TemporaryDirectory() as tmp, mock.patch.object(headless_smoke, "ROOT", Path(tmp)), mock.patch.object(
        headless_smoke, "find_executable", return_value=Path("QEGTRAIN")
    ):
        (Path(tmp) / "tools/e2e").mkdir(parents=True)
        for variable, folder in ((str(Path(tmp) / "build/smoke-logs"), "build/smoke-logs"), ("", "tools/e2e")):
            log = Path(tmp) / folder / "headless_case_2.log"
            for returncode in (0, 1):
                result = subprocess.CompletedProcess([], returncode, f"output {returncode}\n", b"")
                with mock.patch.dict(os.environ, {"QEGTRAIN_SMOKE_LOG_DIR": variable}), mock.patch.object(
                    headless_smoke, "run_command", return_value=result
                ):
                    try:
                        headless_smoke.run_case(2, cwd=Path(tmp), out_base=Path(tmp))
                        message = ""
                    except SystemExit as exc:
                        message = str(exc.code)
                if not log.is_file() or log.read_text(encoding="utf-8") != f"output {returncode}\n":
                    raise SystemExit(f"the log of a case with exit code {returncode} is not {log}")
                if returncode and str(log) not in message:
                    raise SystemExit(f"a failed case does not name its log {log}: {message!r}")


def main() -> None:
    check_position_cases()
    check_executable_lookup()
    check_case_logs()
    for case_id in range(1, 5):
        check_scene_structure(case_id)
    check_scene_matches_netherlands(7)

    command = case_command(3, Path("QEGTRAIN"))
    expected = ["--scene", str(Path(__file__).resolve().parents[2] / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"),
                "-g", "0", "--detailed-trajectories"]
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

    # A trajectory row is the train, three more cells and the positions from second 0 on.
    def trajectory_sample(**ends: int) -> str:
        return "".join(f"{train}\t1\t0\tA\t" + "\t".join("1" for _ in range(end + 1)) + "\n" for train, end in ends.items())

    # A-1 left the network and is judged. A-2 has no departure, A-3 arrived too late to be judged, A-4 never arrived
    # and A-5 departed but is still in the network at second 7999. A-6 has no trajectory row.
    stopped = [
        ("A-1", stations, [120, 1500], [180, 1560]),
        ("A-2", stations, [1920, 3300], [1980, NO_ARRIVAL]),
        ("A-3", stations, [3720, 7700], [3780, NO_ARRIVAL]),
        ("A-4", stations, [5520, NO_ARRIVAL], [5580, NO_ARRIVAL]),
        ("A-5", stations, [5640, 5700], [5700, 5760]),
        ("A-6", stations, [5760, 5800], [5820, 5860]),
    ]
    ended = trajectory_sample(**{"A-1": 1600, "A-2": 7999, "A-3": 7999, "A-4": 7999, "A-5": 7999})
    errors = services_not_ended(timetable_sample(*stopped), ended, 8000)
    expected_errors = [
        "A-2 has no departure from its last stop",
        "A-5 is still in the network at the end of the run",
        "A-6 has no trajectory",
    ]
    if errors != expected_errors:
        raise SystemExit(f"services that did not end were reported as {errors}, expected {expected_errors}")
    # Without a judged train nothing proves that the services end.
    for judged_none in (timetable_sample(*stopped[2:4]), ""):
        errors = services_not_ended(judged_none, ended, 8000)
        if errors != ["no train reached its last stop with time to spare"]:
            raise SystemExit(f"a run without a judged train was reported as {errors}")

    # BlockingTimes.txt holds six lines per train. One record is one section id.
    def blocking_sample(*sections: list[str]) -> str:
        return "".join(
            f"T-{index}\n{' '.join(ids)} \n{'0 ' * len(ids)}\n{'1 ' * len(ids)}\n{'2 ' * len(ids)}\n{'3 ' * len(ids)}\n"
            for index, ids in enumerate(sections, 1)
        )

    for sample, expected_records in (
        (blocking_sample(["@0-B0@", "@1-B0@"], ["@0-B0@"]), 3),
        # A train without a signalling level on its sections has an empty line of section ids.
        (blocking_sample([], ["@0-B0@"], []), 1),
        (blocking_sample([], []), 0),
        ("", 0),
    ):
        records = blocking_time_records(sample)
        if records != expected_records:
            raise SystemExit(f"{records} blocking-time records in {sample!r}, expected {expected_records}")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "BlockingTimes.txt"
        path.write_text(blocking_sample(["@0-B0@"]), encoding="utf-8")
        check_blocking_times("sample", path)
        path.write_text(blocking_sample([], []), encoding="utf-8")
        try:
            check_blocking_times("sample", path)
        except SystemExit as exc:
            if "no section records" not in str(exc.code):
                raise SystemExit(f"a file without section records was reported as {exc.code}")
        else:
            raise SystemExit("a file without section records passed the blocking-time check")

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
