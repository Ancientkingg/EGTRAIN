#!/usr/bin/env python3
"""Import a railML 2 file with scene_tool, validate the scene as runnable and run it headless.

usage: railml_import_smoke.py SCENE_TOOL APP RAILML HORIZON_SECONDS

The test is meant for EGTRAIN/QEGTRAIN/Input/Ostsachsen_V220.railml, the railML 2.2 sample of a real
network. The sample is not in the repository; when RAILML does not exist, the script exits with 77 and
CTest reports the test as skipped. The run must end normally and at least one train must use energy.
The script prints the import time, the size of the scene and the time of the run.
"""
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

RUN_TIMEOUT_SECONDS = 240
SKIPPED = 77


def run_tool(scene_tool: Path, *args: str) -> str:
    """Run scene_tool and return its output; exit when it fails."""
    proc = subprocess.run([str(scene_tool), *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"scene_tool {' '.join(args)} exited with {proc.returncode}\n{proc.stdout[-4000:]}")
    return proc.stdout


def directory_size(path: Path) -> int:
    """The total size of the files below path in bytes."""
    return sum(item.stat().st_size for item in path.rglob("*") if item.is_file())


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    scene_tool = Path(sys.argv[1]).resolve()
    app = Path(sys.argv[2]).resolve()
    railml = Path(sys.argv[3]).resolve()
    horizon = sys.argv[4]
    if not railml.is_file():
        print(f"skipped: {railml} does not exist")
        sys.exit(SKIPPED)

    with tempfile.TemporaryDirectory(prefix="qegtrain-railml-") as temp:
        scene = Path(temp) / "scene"
        started = time.monotonic()
        report = run_tool(scene_tool, "import-railml", str(railml), str(scene))
        import_seconds = time.monotonic() - started
        if "scene.import.railml.signals_generated" not in report:
            raise SystemExit(f"the import did not report the generated signals\n{report[-4000:]}")
        run_tool(scene_tool, "validate", "--runnable", str(scene))
        print(f"import passed in {import_seconds:.2f} s: {directory_size(scene)} bytes in {scene.name}")

        output = Path(temp) / "output"
        env = os.environ.copy()
        env["QEGTRAIN_OUTPUT_DIR"] = str(output)
        env["QT_QPA_PLATFORM"] = "offscreen"
        command = [str(app), "--scene", str(scene), "-h", horizon, "-g", "0", "-pax", "0"]
        started = time.monotonic()
        try:
            proc = subprocess.run(command, cwd=app.parent, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            tail = (exc.stdout or b"").decode("utf-8", errors="replace")[-4000:]
            raise SystemExit(f"headless run timed out after {RUN_TIMEOUT_SECONDS}s\n{tail}") from exc
        run_seconds = time.monotonic() - started
        text = proc.stdout.decode("utf-8", errors="replace")
        if proc.returncode != 0:
            raise SystemExit(f"headless run exited with {proc.returncode}\n{text[-4000:]}")
        if "End of Simulation" not in text:
            raise SystemExit(f"headless run printed no 'End of Simulation'\n{text[-4000:]}")
        energy = list((output / "Output").glob("*/EnergyConsumptionPerTrain.txt"))
        if len(energy) != 1:
            raise SystemExit(f"expected one EnergyConsumptionPerTrain.txt, found {len(energy)}\n{text[-4000:]}")
        rows = [line.split() for line in energy[0].read_text(encoding="utf-8").splitlines()[1:] if line.strip()]
        moved = sum(1 for row in rows if len(row) > 1 and float(row[1]) > 0.0)
        if moved == 0:
            raise SystemExit("no train used energy in the headless run")
        print(f"headless run of {horizon} s passed in {run_seconds:.1f} s: {moved} of {len(rows)} trains moved")


if __name__ == "__main__":
    main()
