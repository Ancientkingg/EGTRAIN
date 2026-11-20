#!/usr/bin/env python3
"""Time headless runs of a scene with a growing number of synthetic passengers.

Each run copies the scene to a temporary folder, replaces passengers.json with N passengers who all board one
service occurrence at one station, and runs EGTRAIN without a window. With --live the journeys have no legs, so that
the run plans each of them on the timetable when the passenger appears. The passenger log is switched off, so the
numbers measure the simulation and not the log output.

Example, from the repository root:

    python3 tools/performance/passenger_scale.py --app build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN \\
        --counts 500 5000 50000 [--live]
"""

import argparse
import json
import os
import platform
import random
import resource
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
RUN_DIR = REPO_ROOT / "EGTRAIN/QEGTRAIN"
DEFAULT_SCENE = RUN_DIR / "Scenes/Paimpol"
# Paimpol: the stops of the all-stops service after its first stop, and the window in which its first occurrence
# leaves Guingamp (07:38).
DEFAULT_ORIGIN = "Guingamp"
DEFAULT_DESTINATIONS = ["Gourland", "Tregonneau_Squiffiec", "Brelidy-Plouec", "Pontrieaux", "Paimpol"]
DEFAULT_SERVICE = "Guin-Paim-ALL-STOPS-1"
DEFAULT_WINDOW = (26000.0, 27300.0)


def write_passengers(scene_dir, count, origin, destinations, service, window, seed, live=False):
    rng = random.Random(seed)
    passengers = []
    for i in range(count):
        destination = rng.choice(destinations)
        journey_id = f"s{i}:1"
        passengers.append({
            "id": f"s{i}",
            "journeys": [{
                "id": journey_id,
                "origin": origin,
                "destination": destination,
                "legs": [] if live else [{"id": f"{journey_id}.leg.1", "origin": origin, "destination": destination,
                                          "service": service, "occurrence": 1}],
                "planned_departure": {"start_seconds": window[0], "end_seconds": window[1]},
                "planned_arrival": {"start_seconds": window[1], "end_seconds": window[1] + 3600.0},
            }],
        })
    with open(scene_dir / "passengers.json", "w", encoding="utf-8") as handle:
        json.dump({"passengers": passengers}, handle)


def peak_rss_mib():
    # The largest resident set of any child so far: bytes on macOS, KiB on Linux. The counts run in ascending order,
    # so after each run it is the peak of that run (or of an earlier, smaller one when memory does not grow).
    scale = 1 if platform.system() == "Darwin" else 1024
    return resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss * scale / (1024 * 1024)


def run_once(app, scene_dir, output_dir, horizon):
    env = dict(os.environ)
    env["EGTRAIN_LOG"] = "sim.pax.info=false"
    env["QEGTRAIN_OUTPUT_DIR"] = str(output_dir)
    env["QEGTRAIN_DISABLE_UPDATES"] = "1"
    command = [str(app), "--scene", str(scene_dir), "-g", "0"]
    if horizon:
        command += ["-h", str(horizon)]
    started = time.perf_counter()
    result = subprocess.run(command, cwd=RUN_DIR, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            text=True, check=False)
    elapsed = time.perf_counter() - started
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(f"run failed with exit code {result.returncode}")
    return elapsed


def completed_journeys(output_dir, scene_name):
    delays = output_dir / "Output" / scene_name / "PassengerStatus" / "JourneyDelays.txt"
    lines = delays.read_text(encoding="utf-8").splitlines()[1:]
    return sum(1 for line in lines if not line.endswith("Journey_not_yet_completed")), len(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", required=True, type=Path, help="the QEGTRAIN executable")
    parser.add_argument("--scene", type=Path, default=DEFAULT_SCENE, help="the case folder to copy")
    parser.add_argument("--counts", type=int, nargs="+", default=[500, 5000, 50000])
    parser.add_argument("--origin", default=DEFAULT_ORIGIN)
    parser.add_argument("--destinations", nargs="+", default=DEFAULT_DESTINATIONS)
    parser.add_argument("--service", default=DEFAULT_SERVICE)
    parser.add_argument("--horizon", type=int, default=0, help="seconds; 0 keeps the duration of the case")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--live", action="store_true", help="write the journeys without legs, so that the run plans them")
    args = parser.parse_args()

    app = args.app.resolve()
    scene_name = json.loads((args.scene / "scene.json").read_text(encoding="utf-8")).get("name", args.scene.name)
    print("passengers seconds peak_rss_mib completed journeys")
    for count in sorted(args.counts):
        with tempfile.TemporaryDirectory(prefix="egtrain-pax-scale-") as temp:
            scene_dir = Path(temp) / "scene"
            output_dir = Path(temp) / "output"
            shutil.copytree(args.scene, scene_dir)
            output_dir.mkdir()
            write_passengers(scene_dir, count, args.origin, args.destinations, args.service, DEFAULT_WINDOW,
                             args.seed, args.live)
            elapsed = run_once(app, scene_dir, output_dir, args.horizon)
            done, total = completed_journeys(output_dir, scene_name)
            print(f"{count} {elapsed:.2f} {peak_rss_mib():.0f} {done} {total}", flush=True)


if __name__ == "__main__":
    main()
