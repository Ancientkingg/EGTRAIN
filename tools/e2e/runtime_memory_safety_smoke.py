#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCENES = ROOT / "EGTRAIN/QEGTRAIN/Scenes"


def run(app: Path, scene: Path, output: Path, label: str) -> None:
    env = os.environ.copy()
    env["QEGTRAIN_OUTPUT_DIR"] = str(output)
    proc = subprocess.run(
        [str(app), "--scene", str(scene), "-h", "3", "-g", "0", "-TSM", "0", "-RC", "0"],
        cwd=output.parent,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=120,
    )
    if proc.returncode != 0:
        raise SystemExit(f"{label} exited with {proc.returncode}\n{proc.stdout[-4000:]}")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: runtime_memory_safety_smoke.py PATH_TO_QEGTRAIN")
    app = Path(sys.argv[1]).resolve()

    with tempfile.TemporaryDirectory() as tmp:
        temp = Path(tmp)
        run(app, SCENES / "Milano_Brescia", temp / "milano-output", "Milano zero-stop runtime")

        paimpol = temp / "Paimpol-time-zero"
        shutil.copytree(SCENES / "Paimpol", paimpol)
        services_path = paimpol / "services.json"
        services = json.loads(services_path.read_text(encoding="utf-8"))
        services["services"][1]["entry_time_seconds"] = 0.0
        services["services"][1]["repeat"]["headway_seconds"] = 1.0
        services_path.write_text(json.dumps(services, indent=2) + "\n", encoding="utf-8")
        run(app, paimpol, temp / "paimpol-output", "Paimpol time-zero runtime")


if __name__ == "__main__":
    main()
