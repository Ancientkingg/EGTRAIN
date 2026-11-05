#!/usr/bin/env python3
"""Start QEGTRAIN headless on a scene three times and check how each run ended.

usage: headless_scene_smoke.py APP SCENE [HORIZON_SECONDS]

The first run uses the default options and an output folder whose name is not
ASCII. It must finish and must not write the detailed trajectory files,
TEMP/Traj_Train_*.txt and TrainTrajectories/TrainPathDiagram.txt. The second
run goes to the same folder, where a folder takes the place of
TrainTrajectories/BlockingTimes.txt: it must exit with 1 and name that file on
standard error. The third run adds --detailed-trajectories, into a new output
folder, and must write them.

APP runs from its own directory, so SCENE may be relative to it (Scenes/Paimpol
next to the executable on Windows and Linux). QEGTRAIN is a GUI-subsystem
program on Windows: it writes only to redirected handles and a script must wait
for the process itself, so this reads the pipes and the exit code directly.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

RUN_TIMEOUT_SECONDS = 120
# Windows reads a narrow path in the system code page, which holds none of these characters in most locales.
NON_ASCII_FOLDER = "out-Zo\u00eb-Malm\u00f6-\u6771\u4eac"


def headless_command(app: Path, scene: str, horizon: str, output: Path, options: list[str]) -> tuple[list[str], dict]:
    """The command line and the environment of a headless run of the scene into the output folder."""
    env = os.environ.copy()
    env["QEGTRAIN_OUTPUT_DIR"] = str(output)
    env["QT_QPA_PLATFORM"] = "offscreen"
    return [str(app), "--scene", scene, "-h", horizon, "-g", "0", "-pax", "0", *options], env


def run_scene(app: Path, scene: str, horizon: str, output: Path, options: list[str]) -> tuple[Path, int]:
    """Run the scene into the output folder and return the folder of the run and the size of its output."""
    command, env = headless_command(app, scene, horizon, output, options)
    try:
        proc = subprocess.run(command, cwd=app.parent, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as exc:
        tail = (exc.stdout or b"").decode("utf-8", errors="replace")[-4000:]
        raise SystemExit(f"headless run timed out after {RUN_TIMEOUT_SECONDS}s\n{tail}") from exc
    text = proc.stdout.decode("utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"headless run exited with {proc.returncode}\n{text[-4000:]}")
    if "End of Simulation" not in text:
        raise SystemExit(f"headless run printed no 'End of Simulation'\n{text[-4000:]}")
    energy = list((output / "Output").glob("*/EnergyConsumptionPerTrain.txt"))
    if len(energy) != 1:
        raise SystemExit(f"expected one EnergyConsumptionPerTrain.txt, found {len(energy)}\n{text[-4000:]}")
    return energy[0].parent, len(text)


def run_blocked_output(app: Path, scene: str, horizon: str, output: Path, run: Path) -> None:
    """Run the scene again into the folder of an earlier run, where a folder takes the place of BlockingTimes.txt."""
    blocked = run / "TrainTrajectories" / "BlockingTimes.txt"
    blocked.unlink()
    blocked.mkdir()
    command, env = headless_command(app, scene, horizon, output, [])
    try:
        proc = subprocess.run(command, cwd=app.parent, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=RUN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"headless run with a blocked file timed out after {RUN_TIMEOUT_SECONDS}s") from exc
    errors = proc.stderr.decode("utf-8", errors="replace")
    if proc.returncode != 1:
        raise SystemExit(f"headless run with a blocked file exited with {proc.returncode}, not 1\n{errors[-4000:]}")
    # The application joins the parts of the path with '/' on every system.
    if not any(line.startswith("ERROR: Cannot write ") and "TrainTrajectories/BlockingTimes.txt: " in line
               for line in errors.splitlines()):
        raise SystemExit(f"headless run with a blocked file did not name it on standard error\n{errors[-4000:]}")
    if b"End of Simulation" in proc.stdout:
        raise SystemExit("headless run with a blocked file printed 'End of Simulation'")


def main() -> None:
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    app = Path(sys.argv[1]).resolve()
    scene = sys.argv[2]
    horizon = sys.argv[3] if len(sys.argv) == 4 else "300"
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")

    with tempfile.TemporaryDirectory(prefix="qegtrain-headless-") as temp:
        run, size = run_scene(app, scene, horizon, Path(temp) / NON_ASCII_FOLDER, [])
        # TimetablePoints.txt is written right after TrainPathDiagram.txt and before the trajectory files.
        if not (run / "TrainTrajectories" / "TimetablePoints.txt").is_file():
            raise SystemExit("the default run wrote no TrainTrajectories/TimetablePoints.txt")
        if (run / "TEMP").exists():
            raise SystemExit("the default run created the folder TEMP")
        if (run / "TrainTrajectories" / "TrainPathDiagram.txt").exists():
            raise SystemExit("the default run wrote TrainTrajectories/TrainPathDiagram.txt")
        print(f"headless run passed: {scene}, {size} bytes of output, {run.name}")

        run_blocked_output(app, scene, horizon, Path(temp) / NON_ASCII_FOLDER, run)
        print(f"headless run with a result file that cannot be written failed as expected: {scene}")

        run, size = run_scene(app, scene, horizon, Path(temp) / "out-detailed", ["--detailed-trajectories"])
        if not (run / "TrainTrajectories" / "TrainPathDiagram.txt").is_file():
            raise SystemExit("the run with --detailed-trajectories wrote no TrainTrajectories/TrainPathDiagram.txt")
        if not list((run / "TEMP").glob("Traj_Train_*.txt")):
            raise SystemExit("the run with --detailed-trajectories wrote no TEMP/Traj_Train_*.txt")
        print(f"headless run with --detailed-trajectories passed: {scene}, {size} bytes of output, {run.name}")


if __name__ == "__main__":
    main()
