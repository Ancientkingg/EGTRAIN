#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENE_TOOL = ROOT / "build/scene_tool"
APP = ROOT / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes"

CASES_INFO = [
    (1, "Netherlands"),
    (2, "Paimpol"),
    (3, "Copenhagen"),
    (4, "Milano_Brescia"),
    (5, "Assignment_Gvc_Gdg_Ut"),
    (6, "Lebanon"),
]

IMPORT_NAMES = {4: "Brescia", 5: "Assignment"}


def scene_counts(scene_dir: Path) -> dict[str, int]:
    def read(name: str) -> dict:
        path = scene_dir / name
        return json.loads(path.read_text()) if path.exists() else {}

    infrastructure = read("infrastructure.json")
    stations = read("stations.json").get("stations", [])
    signalling = read("signalling.json")
    rolling_stock = read("rolling_stock.json")
    services = read("services.json")
    passengers = read("passengers.json").get("passengers", [])
    scenarios = read("scenarios.json").get("scenarios", [])
    journeys = [journey for passenger in passengers for journey in passenger.get("journeys", [])]
    return {
        **{key: len(infrastructure.get(key, [])) for key in ("tracks", "nodes", "arcs", "blocks", "connections")},
        "stations": len(stations),
        "platforms": sum(len(station.get("platforms", [])) for station in stations),
        "routes": len(signalling.get("routes", [])),
        "corridors": sum("corridor" in route for route in signalling.get("routes", [])),
        "single_track_restrictions": len(signalling.get("single_track_restrictions", [])),
        "station_boundaries": len(signalling.get("station_boundaries", [])),
        "train_units": len(rolling_stock.get("train_units", [])),
        "compositions": len(rolling_stock.get("compositions", [])),
        "services": len(services.get("services", [])),
        "passengers": len(passengers),
        "journeys": len(journeys),
        "passenger_legs": sum(len(journey.get("legs", [])) for journey in journeys),
        "scenarios": len(scenarios),
    }


def main() -> None:
    if not SCENE_TOOL.exists():
        sys.exit(f"scene_tool not found at {SCENE_TOOL}. Build it first.")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        for case_id, scene_name in CASES_INFO:
            print(f"--- round-trip case {case_id}: {scene_name} ---")
            scene_dir = SCENE_DIR / scene_name
            exported_dir = tmp_dir / f"exported_{case_id}"

            print("Validating committed scene...")
            subprocess.run([str(SCENE_TOOL), "validate", str(scene_dir)], check=True)

            print("Exporting committed scene...")
            subprocess.run([str(SCENE_TOOL), "export", str(scene_dir), str(exported_dir)], check=True)

            reimported_dir = tmp_dir / f"reimported_{case_id}"
            print("Reimporting and validating compatibility output...")
            subprocess.run(
                [str(SCENE_TOOL), "import", str(exported_dir), str(reimported_dir), IMPORT_NAMES.get(case_id, scene_name)],
                check=True,
            )
            subprocess.run([str(SCENE_TOOL), "validate", str(reimported_dir)], check=True)
            expected = scene_counts(scene_dir)
            actual = scene_counts(reimported_dir)
            if expected != actual:
                differences = ", ".join(
                    f"{key} {expected[key]}->{actual[key]}" for key in expected if expected[key] != actual[key]
                )
                raise RuntimeError(f"case {case_id} compatibility count mismatch: {differences}")

        # One executable round trip is enough after all six structural/count checks.
        reimported_dir = tmp_dir / "reimported_5"
        print("Running reimported Assignment scene...")
        env = os.environ.copy()
        env["QEGTRAIN_OUTPUT_DIR"] = str(tmp_dir / "run")
        subprocess.run(
            [str(APP), "--scene", str(reimported_dir), "-h", "2", "-g", "0", "-TSM", "0", "-RC", "0"],
            cwd=tmp_dir,
            env=env,
            check=True,
            timeout=120,
        )

    print("ROUNDTRIP PASS")


if __name__ == "__main__":
    main()
