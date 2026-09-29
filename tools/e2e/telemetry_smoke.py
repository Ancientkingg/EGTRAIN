#!/usr/bin/env python3
"""Offscreen running-app smoke with an isolated, refusing numeric-loopback receiver."""
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"
PORT = 19497


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: tools/e2e/telemetry_smoke.py /path/to/QEGTRAIN")
    executable = Path(sys.argv[1]).resolve(strict=True)
    directory = tempfile.mkdtemp(prefix="egtrain-telemetry-smoke-")
    passed = False
    try:
        root = Path(directory)
        scene = root / SCENE.name
        shutil.copytree(SCENE, scene)
        scene_json = scene / "scene.json"
        data = json.loads(scene_json.read_text(encoding="utf-8"))
        data["simulation_settings"]["duration_seconds"] = 40000.0
        scene_json.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        receiver = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        receiver.bind(("127.0.0.1", PORT))  # Fail if the fixed mock port is occupied.
        receiver.listen(8)
        receiver.settimeout(0.2)
        attempts = []
        stopping = threading.Event()

        def refuse() -> None:
            while not stopping.is_set():
                try:
                    conn, address = receiver.accept()
                except socket.timeout:
                    continue
                except OSError:
                    return
                if address[0] == "127.0.0.1":
                    attempts.append(1)
                conn.close()

        thread = threading.Thread(target=refuse, daemon=True)
        thread.start()
        env = os.environ.copy()
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["QEGTRAIN_AUTOSTART"] = "1"
        env["QEGTRAIN_E2E_TELEMETRY_SMOKE"] = directory
        env["QEGTRAIN_E2E_SETTINGS_DIR"] = str(root / "other-settings")
        env["QEGTRAIN_OUTPUT_DIR"] = str(root / "output")
        output = root / "app.log"
        args = [str(executable), "--scene", str(scene), "-h", "200", "-g", "1", "-pax", "0",
                "-TSM", "0", "-RC", "0"]
        try:
            with output.open("wb") as log:
                proc = subprocess.Popen(args, cwd=ROOT / "EGTRAIN/QEGTRAIN", env=env,
                                        stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                startup_deadline = time.monotonic() + 300
                shutdown_deadline = None
                while proc.poll() is None:
                    if shutdown_deadline is None and b"E2E_GUI_RUN_RESULTS" in output.read_bytes():
                        shutdown_deadline = time.monotonic() + 12
                    if time.monotonic() > (shutdown_deadline or startup_deadline):
                        raise AssertionError("offscreen simulation or post-results shutdown exceeded its deadline")
                    time.sleep(0.1)
                rc = proc.returncode
            text = output.read_text(encoding="utf-8", errors="replace")
            if rc != 0:
                raise AssertionError(f"offscreen application returned {rc}: {text[-2000:]}")
            for line in text.splitlines():
                if line.startswith("E2E_TELEMETRY_MOCK_REFUSED "):
                    raise AssertionError(line)
            for marker in ("E2E_GUI_AUTOSTART_RUNNING", "E2E_GUI_RUN_RESULTS", "E2E_TELEMETRY_MOCK_ACCEPTED",
                           "E2E_TELEMETRY_SHUTDOWN"):
                if marker not in text:
                    raise AssertionError(f"missing {marker}: {text[-2000:]}")
            if not attempts or not (root / "queue" / "usage.json").is_file():
                raise AssertionError("no real dead-receiver attempt or no private usage queue")
            saved = root / "output" / "Output" / scene.name
            for name in ("EnergyConsumptionPerTrain.txt", "TotalEnergyConsumption.txt"):
                if not (saved / name).is_file():
                    raise AssertionError(f"simulation output missing: {name}")
            print("offscreen startup, simulation, saved output and graceful shutdown with dead numeric-loopback receiver: passed")
            passed = True
        finally:
            stopping.set()
            receiver.close()
            thread.join(timeout=2)
            if "proc" in locals() and proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=5)
    finally:
        if passed:
            shutil.rmtree(directory)
        else:
            print(f"telemetry smoke diagnostics retained: {directory}", file=sys.stderr)


if __name__ == "__main__":
    main()
