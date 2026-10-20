#!/usr/bin/env python3
"""Start QEGTRAIN headless on a scene twice and check that each run finished.

usage: headless_scene_smoke.py APP SCENE [HORIZON_SECONDS]

The first run uses the default options and must not write the detailed
trajectory files, TEMP/Traj_Train_*.txt and TrainTrajectories/TrainPathDiagram.txt.
The second run adds --detailed-trajectories, into a new output folder, and must
write them.

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


def run_scene(app: Path, scene: str, horizon: str, output: Path, options: list[str]) -> tuple[Path, int]:
    """Run the scene into the output folder and return the folder of the run and the size of its output."""
    env = os.environ.copy()
    env["QEGTRAIN_OUTPUT_DIR"] = str(output)
    env["QT_QPA_PLATFORM"] = "offscreen"
    command = [str(app), "--scene", scene, "-h", horizon, "-g", "0", "-pax", "0", "-TSM", "0", "-RC", "0", *options]
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


def main() -> None:
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    app = Path(sys.argv[1]).resolve()
    scene = sys.argv[2]
    horizon = sys.argv[3] if len(sys.argv) == 4 else "300"
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")

    with tempfile.TemporaryDirectory(prefix="qegtrain-headless-") as temp:
        run, size = run_scene(app, scene, horizon, Path(temp) / "out", [])
        # TimetablePoints.txt is written right after TrainPathDiagram.txt and before the trajectory files.
        if not (run / "TrainTrajectories" / "TimetablePoints.txt").is_file():
            raise SystemExit("the default run wrote no TrainTrajectories/TimetablePoints.txt")
        if (run / "TEMP").exists():
            raise SystemExit("the default run created the folder TEMP")
        if (run / "TrainTrajectories" / "TrainPathDiagram.txt").exists():
            raise SystemExit("the default run wrote TrainTrajectories/TrainPathDiagram.txt")
        print(f"headless run passed: {scene}, {size} bytes of output, {run.name}")

        run, size = run_scene(app, scene, horizon, Path(temp) / "out-detailed", ["--detailed-trajectories"])
        if not (run / "TrainTrajectories" / "TrainPathDiagram.txt").is_file():
            raise SystemExit("the run with --detailed-trajectories wrote no TrainTrajectories/TrainPathDiagram.txt")
        if not list((run / "TEMP").glob("Traj_Train_*.txt")):
            raise SystemExit("the run with --detailed-trajectories wrote no TEMP/Traj_Train_*.txt")
        print(f"headless run with --detailed-trajectories passed: {scene}, {size} bytes of output, {run.name}")


if __name__ == "__main__":
    main()
