#!/usr/bin/env python3
"""Start QEGTRAIN headless on a scene and check that the run finished.

usage: headless_scene_smoke.py APP SCENE [HORIZON_SECONDS]

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


def main() -> None:
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    app = Path(sys.argv[1]).resolve()
    scene = sys.argv[2]
    horizon = sys.argv[3] if len(sys.argv) == 4 else "300"
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")

    with tempfile.TemporaryDirectory(prefix="qegtrain-headless-") as temp:
        output = Path(temp) / "out"
        env = os.environ.copy()
        env["QEGTRAIN_OUTPUT_DIR"] = str(output)
        env["QT_QPA_PLATFORM"] = "offscreen"
        command = [str(app), "--scene", scene, "-h", horizon, "-g", "0", "-pax", "0", "-TSM", "0", "-RC", "0"]
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
        print(f"headless run passed: {scene}, {len(text)} bytes of output, {energy[0].parent.name}")


if __name__ == "__main__":
    main()
