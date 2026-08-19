#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Milano_Brescia"
FIXED_SEED = ROOT / "tools/golden_master/fixed_seed.seed"
SCENE_OUTPUT_FILES = (
    Path("TrainTrajectories/TrainServicePathDiagram.txt"),
    Path("TrainTrajectories/TimetablePoints.txt"),
    Path("TrainTrajectories/Stats_Stations.txt"),
    Path("EnergyConsumptionPerTrain.txt"),
)
RUN_TIMEOUT = 240


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: simulation_determinism_smoke.py PATH_TO_QEGTRAIN")

    app = Path(sys.argv[1]).resolve()
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")
    if not SCENE.is_dir():
        raise SystemExit(f"canonical scene not found: {SCENE}")
    if not FIXED_SEED.is_file():
        raise SystemExit(f"fixed seed not found: {FIXED_SEED}")

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
            shutil.copyfile(FIXED_SEED, run_root / "rand1.seed")

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


if __name__ == "__main__":
    main()
