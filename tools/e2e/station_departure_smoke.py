#!/usr/bin/env python3
"""Run one all-stops service on Netherlands route30 and check that it leaves Weesp.

usage: station_departure_smoke.py PATH_TO_QEGTRAIN

A train parked at a stop must end its dwell and depart, and its timetable point
must report the arrival and the departure, even when the parked position rounds
one unit in the last place away from the stopping point. That rounding needs
fused multiply and add, which arm64 compilers use; it happens at Weesp on
route30. On other targets this script passes without exercising it, and the
cases in test_operationsbuilder cover the comparisons there.

The run prints no per-step line by default and one progress line in 100 steps. A second run with
EGTRAIN_LOG=sim.step=true prints the line "<train> is stopping at <station>" for the last stop.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Netherlands"
RUN_TIMEOUT = 240
NOT_RECORDED = -10000.0
TRAIN = "SPR_461-1"

# The all-stops pattern of the historical timetable file TimeTable/SprAsdBrn.txt
# (commit ee2b40d): station and dwell seconds.
STOPS = (
    ("Asd", 60.0),
    ("Asdm", 42.0),
    ("Assp", 42.0),
    ("Dmn", 42.0),
    ("Wp", 60.0),
    ("Ndb", 42.0),
    ("Bsmz", 42.0),
    ("Hvsm", 42.0),
    ("Hvs", 60.0),
    ("Brn", 42.0),
)


def service_document() -> dict:
    stops = [{"dwell_seconds": dwell, "station": station} for station, dwell in STOPS]
    return {
        "services": [
            {
                "composition": "SLT06",
                "entry_time_seconds": 100.0,
                "id": "SPR_461",
                "operating_code": "SPR_461",
                "route": "route30",
                "stops": stops,
            }
        ]
    }


def run_service(app: Path, scene: Path, run_root: Path, logging_rules: str = "") -> tuple:
    """Run the app headless on the scene and return (output folder, combined stdout and stderr)."""
    output = run_root / "output"
    output.mkdir(parents=True)

    env = os.environ.copy()
    env.pop("EGTRAIN_LOG", None)
    env.update(
        {
            "QT_QPA_PLATFORM": "offscreen",
            "QEGTRAIN_OUTPUT_DIR": str(output),
        }
    )
    if logging_rules:
        env["EGTRAIN_LOG"] = logging_rules
    command = [str(app), "--scene", str(scene), "-g", "0", "-TSM", "0", "-RC", "0"]
    try:
        process = subprocess.run(
            command,
            cwd=run_root,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=RUN_TIMEOUT,
        )
    except subprocess.TimeoutExpired as exc:
        tail = exc.stdout or ""
        if isinstance(tail, bytes):
            tail = tail.decode("utf-8", errors="replace")
        raise SystemExit(f"station departure run timed out after {RUN_TIMEOUT}s\n{tail[-4000:]}") from exc
    if process.returncode != 0:
        raise SystemExit(f"station departure run exited with {process.returncode}\n{process.stdout[-4000:]}")
    return output, process.stdout


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: station_departure_smoke.py PATH_TO_QEGTRAIN")

    app = Path(sys.argv[1]).resolve()
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")
    if not SCENE.is_dir():
        raise SystemExit(f"canonical scene not found: {SCENE}")

    with tempfile.TemporaryDirectory(prefix="qegtrain-departure-") as temp:
        temp_root = Path(temp)
        scene = temp_root / "scene"
        shutil.copytree(SCENE, scene)
        (scene / "services.json").write_text(
            json.dumps(service_document(), indent=4), encoding="utf-8"
        )
        scene_document = json.loads((scene / "scene.json").read_text(encoding="utf-8"))
        scene_name = scene_document["name"]
        run_end = scene_document["simulation_settings"]["duration_seconds"]

        output, stdout = run_service(app, scene, temp_root / "run")

        points = output / "Output" / scene_name / "TrainTrajectories" / "TimetablePoints.txt"
        if not points.is_file():
            raise SystemExit(f"missing timetable points: {points}")
        lines = points.read_text(encoding="utf-8", errors="replace").splitlines()
        if len(lines) < 6 or lines[0].split()[:1] != [TRAIN]:
            raise SystemExit(f"unexpected timetable points for {TRAIN}:\n" + "\n".join(lines[:6]))
        stations = lines[1].split()
        arrivals = [float(value) for value in lines[4].split()]
        departures = [float(value) for value in lines[5].split()]
        if (
            stations != [station for station, _ in STOPS]
            or len(arrivals) != len(stations)
            or len(departures) != len(stations)
        ):
            raise SystemExit("timetable points do not list the stops of the service:\n" + "\n".join(lines[:6]))

        weesp = stations.index("Wp")
        if departures[weesp] == NOT_RECORDED:
            raise SystemExit(f"{TRAIN} did not leave Wp")
        print(f"PASS {TRAIN} left Wp at {departures[weesp]:.0f} s")

        # Every stop is reported with its own arrival and a departure at least the dwell time later.
        for index, (station, dwell) in enumerate(STOPS):
            if arrivals[index] == NOT_RECORDED or departures[index] == NOT_RECORDED:
                raise SystemExit(f"{TRAIN} has no recorded stop at {station}:\n" + "\n".join(lines[:6]))
            if departures[index] - arrivals[index] < dwell:
                raise SystemExit(
                    f"{TRAIN} at {station}: arrival {arrivals[index]:.0f} s and departure "
                    f"{departures[index]:.0f} s are less than the dwell of {dwell:.0f} s apart"
                )
        print(f"PASS {TRAIN} reports arrival and departure at {len(STOPS)} stops")

        if "is stopping at" in stdout:
            raise SystemExit(f"the run prints a per-step line by default\n{stdout[-4000:]}")
        # The progress line is printed once in 100 steps, not once per step.
        progress_lines = stdout.count("Time of simulation is")
        if not 0 < progress_lines <= run_end // 50:
            raise SystemExit(f"{progress_lines} progress lines in a run of {run_end} s, expected about one in 100 s\n{stdout[-4000:]}")
        _, verbose_stdout = run_service(app, scene, temp_root / "run-verbose", "sim.step=true")
        if f"{TRAIN} is stopping at Brn" not in verbose_stdout:
            raise SystemExit(f"{TRAIN} never arrived at Brn\n{verbose_stdout[-4000:]}")
        print("PASS the per-step line is off by default and on with EGTRAIN_LOG=sim.step=true")

        # The last stop has no planned departure, so the train leaves the network after its dwell:
        # its trajectory ends before the end of the run.
        path = output / "Output" / scene_name / "TrainTrajectories" / "TrainServicePathDiagram.txt"
        if not path.is_file():
            raise SystemExit(f"missing trajectory file: {path}")
        samples = []
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            cells = line.split("\t")
            if cells[0] == TRAIN:
                samples = [time_seconds for time_seconds, cell in enumerate(cells[4:]) if cell]
        if not samples:
            raise SystemExit(f"{TRAIN} has no trajectory")
        if samples[-1] >= run_end - 1:
            raise SystemExit(f"{TRAIN} was still in the network at the end of the run, after leaving Brn at {departures[-1]:.0f} s")
        print(f"PASS {TRAIN} left Brn at {departures[-1]:.0f} s and the network at {samples[-1]} s")


if __name__ == "__main__":
    main()
