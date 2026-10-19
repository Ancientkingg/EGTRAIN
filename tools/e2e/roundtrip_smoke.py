#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from headless_smoke import check_blocking_times, find_executable

ROOT = Path(__file__).resolve().parents[2]
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes"

CASES_INFO = [
    (1, "Netherlands"),
    (2, "Paimpol"),
    (3, "Copenhagen"),
    (4, "Milano_Brescia"),
    (5, "Assignment_Gvc_Gdg_Ut"),
    (7, "Amsterdam_Hilversum_Student"),
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


def check_signalling_areas(case_id: int, scene_name: str, scene_dir: Path, exported_dir: Path, reimported_dir: Path) -> None:
    """The sidecar preserves the canonical assignment under the exported track mapping."""
    label = f"case {case_id} ({scene_name}) signalling area mismatch"
    areas = json.loads((scene_dir / "signalling.json").read_text()).get("signalling_areas", [])
    tracks = json.loads((scene_dir / "infrastructure.json").read_text()).get("tracks", [])
    def is_legacy_id(track_id: str) -> bool:
        return len(track_id) > 1 and track_id[0] == "B" and all("0" <= c <= "9" for c in track_id[1:])

    used = {track["id"] for track in tracks if is_legacy_id(track["id"])}
    track_ids = {}
    fallback = 0
    for track in tracks:
        track_id = track["id"]
        mapped = track_id
        if not is_legacy_id(track_id):
            while f"B{fallback}" in used:
                fallback += 1
            mapped = f"B{fallback}"
            fallback += 1
            used.add(mapped)
        track_ids[track_id] = mapped
    expected = [dict(area) for area in areas]
    for area in expected:
        if area.get("track"):
            area["track"] = track_ids[area["track"]]
        else:
            area.pop("track", None)
    sidecar_path = exported_dir / "TrackLines/EGTRAINSignallingAreas.json"
    sidecar = json.loads(sidecar_path.read_text())
    if sidecar != {"format_version": 1, "signalling_areas": expected}:
        raise RuntimeError(f"{label}: sidecar {sidecar} does not match mapped assignment {expected}")
    areas_file = exported_dir / "TrackLines/AreasCaseStudy.txt"
    rows = [line.split("\t") for line in areas_file.read_text().splitlines() if line.strip()] if areas_file.exists() else []
    if len(rows) != len(areas) or (not areas and areas_file.exists()):
        raise RuntimeError(f"{label}: {len(rows)} exported rows for {len(areas)} areas")
    for index, (area, row) in enumerate(zip(expected, rows)):
        columns = 5 if area.get("track") else 4
        if (
            len(row) != columns
            or float(row[1]) != float(area["start_km"])
            or float(row[2]) != float(area["end_km"])
            or float(row[3]) != float(area["level"])
            or (columns == 5 and row[4] != area["track"][1:])
        ):
            raise RuntimeError(f"{label}: row {index + 1} {row} does not match area {area}")

    reimported = json.loads((reimported_dir / "signalling.json").read_text())
    expected_import = [dict(area) for area in expected]
    for area in expected_import:
        if area.get("track"):
            area["track"] = f"B{int(area['track'][1:])}"
    if reimported.get("signalling_areas") != expected_import:
        raise RuntimeError(f"{label}: reimported assignment {reimported.get('signalling_areas')} != {expected_import}")
    report = json.loads((reimported_dir / "scene.json").read_text()).get("import_report", [])
    report_rows = [row for row in report if row["category"] == "signalling.areas"]
    if len(report_rows) != 1 or not (
        Path(report_rows[0]["source_file"]) == sidecar_path
        and report_rows[0]["source_count"] == report_rows[0]["converted_count"] == len(expected)
        and report_rows[0]["skipped_count"] == report_rows[0]["unresolved_references"] == 0
    ):
        raise RuntimeError(f"{label}: import report rows {report_rows} for {len(expected)} canonical areas")


def main() -> None:
    scene_tool = find_executable("scene_tool", "QEGTRAIN_SCENE_TOOL")
    app = find_executable("QEGTRAIN", "QEGTRAIN_APP")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        for case_id, scene_name in CASES_INFO:
            print(f"--- round-trip case {case_id}: {scene_name} ---")
            scene_dir = SCENE_DIR / scene_name
            exported_dir = tmp_dir / f"exported_{case_id}"

            print("Validating committed scene...")
            subprocess.run([str(scene_tool), "validate", str(scene_dir)], check=True)

            print("Exporting committed scene...")
            subprocess.run([str(scene_tool), "export", str(scene_dir), str(exported_dir)], check=True)

            reimported_dir = tmp_dir / f"reimported_{case_id}"
            print("Reimporting and validating compatibility output...")
            subprocess.run(
                [str(scene_tool), "import", str(exported_dir), str(reimported_dir), IMPORT_NAMES.get(case_id, scene_name)],
                check=True,
            )
            subprocess.run([str(scene_tool), "validate", str(reimported_dir)], check=True)
            expected = scene_counts(scene_dir)
            actual = scene_counts(reimported_dir)
            if expected != actual:
                differences = ", ".join(
                    f"{key} {expected[key]}->{actual[key]}" for key in expected if expected[key] != actual[key]
                )
                raise RuntimeError(f"case {case_id} compatibility count mismatch: {differences}")
            check_signalling_areas(case_id, scene_name, scene_dir, exported_dir, reimported_dir)

        # One executable round trip is enough after all six structural/count checks.
        reimported_dir = tmp_dir / "reimported_5"
        print("Running reimported Assignment scene...")
        env = os.environ.copy()
        env["QEGTRAIN_OUTPUT_DIR"] = str(tmp_dir / "run")
        proc = subprocess.run(
            [str(app), "--scene", str(reimported_dir), "-g", "0", "-TSM", "0", "-RC", "0"],
            cwd=tmp_dir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=120,
        )
        output = proc.stdout.decode("utf-8", errors="replace")
        sys.stdout.write(output)
        if proc.returncode != 0:
            raise RuntimeError(f"the reimported Assignment run exited with {proc.returncode}")
        if "[scene.signalling.level.missing]" in output:
            raise RuntimeError("the reimported Assignment run has route sections without a signalling level")
        name = json.loads((reimported_dir / "scene.json").read_text())["name"]
        check_blocking_times("reimported Assignment", tmp_dir / "run/Output" / name / "TrainTrajectories/BlockingTimes.txt")

    print("ROUNDTRIP PASS")


if __name__ == "__main__":
    main()
