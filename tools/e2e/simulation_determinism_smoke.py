#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Milano_Brescia"
SCENE_OUTPUT_FILES = (
    Path("TrainTrajectories/TrainServicePathDiagram.txt"),
    Path("TrainTrajectories/TimetablePoints.txt"),
    Path("TrainTrajectories/Stats_Stations.txt"),
    Path("EnergyConsumptionPerTrain.txt"),
)
PASSENGER_SCENE = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Paimpol"
PASSENGER_OUTPUT_FILES = (
    Path("PassengerStatus/JourneyDelays.txt"),
    Path("PassengerStatus/PassengerStatus.txt"),
)
RUN_TIMEOUT = 240


def run_app(app: Path, scene: Path, run_root: Path, extra: list[str]) -> subprocess.CompletedProcess:
    output = run_root / "output"
    output.mkdir(parents=True)
    env = os.environ.copy()
    env.update({"OMP_NUM_THREADS": "4", "QT_QPA_PLATFORM": "offscreen", "QEGTRAIN_OUTPUT_DIR": str(output)})
    command = [str(app), "--scene", str(scene), "-g", "0", "-TSM", "0", "-RC", "0", *extra]
    return subprocess.run(command, cwd=run_root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace", timeout=RUN_TIMEOUT)


def check_seed(app: Path, temp_root: Path) -> None:
    rejected = run_app(app, PASSENGER_SCENE, temp_root / "seed_rejected", ["--seed", "0"])
    if rejected.returncode == 0 or "--seed requires" not in rejected.stdout:
        raise SystemExit(f"--seed 0 was not rejected\n{rejected.stdout[-2000:]}")
    print("PASS --seed 0 rejected")

    texts = {}
    for name, seed in (("first", "5"), ("second", "5"), ("other", "6")):
        run_root = temp_root / f"seed_{name}"
        process = run_app(app, PASSENGER_SCENE, run_root, ["--seed", seed])
        if process.returncode != 0:
            raise SystemExit(f"seed run {name} exited with {process.returncode}\n{process.stdout[-4000:]}")
        scene_name = json.loads((PASSENGER_SCENE / "scene.json").read_text(encoding="utf-8"))["name"]
        texts[name] = [(run_root / "output" / "Output" / scene_name / path).read_bytes()
                       for path in PASSENGER_OUTPUT_FILES]
    if texts["first"] != texts["second"]:
        raise SystemExit("equal seeds gave different passenger output")
    if texts["first"] == texts["other"]:
        raise SystemExit("different seeds gave equal passenger output")
    print("PASS equal seeds give equal passenger output, different seeds do not")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: simulation_determinism_smoke.py PATH_TO_QEGTRAIN")

    app = Path(sys.argv[1]).resolve()
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")
    if not SCENE.is_dir():
        raise SystemExit(f"canonical scene not found: {SCENE}")

    try:
        scene_name = json.loads((SCENE / "scene.json").read_text(encoding="utf-8"))["name"]
    except (OSError, KeyError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read canonical scene name from {SCENE / 'scene.json'}: {exc}") from exc

    with tempfile.TemporaryDirectory(prefix="qegtrain-determinism-") as temp:
        temp_root = Path(temp)
        result_roots: list[Path] = []
        for run_number in (1, 2):
            run_root = temp_root / f"run_{run_number}"
            output = run_root / "output"
            output.mkdir(parents=True)

            env = os.environ.copy()
            env.update(
                {
                    "OMP_NUM_THREADS": "4",
                    "QT_QPA_PLATFORM": "offscreen",
                    "QEGTRAIN_OUTPUT_DIR": str(output),
                }
            )
            command = [
                str(app),
                "--scene",
                str(SCENE),
                "-h",
                "1200",
                "-g",
                "0",
                "-TSM",
                "0",
                "-RC",
                "0",
            ]
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
                output_text = exc.stdout or ""
                raise SystemExit(
                    f"determinism run {run_number} timed out after {RUN_TIMEOUT}s\n{output_text[-4000:]}"
                ) from exc
            if process.returncode != 0:
                raise SystemExit(
                    f"determinism run {run_number} exited with {process.returncode}\n{process.stdout[-4000:]}"
                )

            result_root = output / "Output" / scene_name
            if not result_root.is_dir():
                raise SystemExit(f"determinism run {run_number} missing output directory: {result_root}")
            missing = [str(path) for path in SCENE_OUTPUT_FILES if not (result_root / path).is_file()]
            if missing:
                raise SystemExit(f"determinism run {run_number} missing output files: {', '.join(missing)}")
            result_roots.append(result_root)
            print(f"PASS determinism run {run_number}: {result_root}")

        for relative_path in SCENE_OUTPUT_FILES:
            expected = (result_roots[0] / relative_path).read_bytes()
            actual = (result_roots[1] / relative_path).read_bytes()
            if expected != actual:
                raise SystemExit(f"simulation determinism mismatch: {relative_path}")
            print(f"PASS identical output: {relative_path}")

        check_seed(app, temp_root)


if __name__ == "__main__":
    main()
