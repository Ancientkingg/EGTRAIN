#!/usr/bin/env python3
import json
import subprocess
import tempfile
import sys
from pathlib import Path
from headless_smoke import run_case, check_movement, check_station_arrivals

ROOT = Path(__file__).resolve().parents[2]
SCENE_TOOL = ROOT / "build/scene_tool"
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut"
CASE_ID = 5
STAGED_NAME = "Input_EGTRAIN_Assignment"
EXPECTED_IDS = ("IC1723", "S19825", "IC2025", "S9827")
EXPECTED_ROUTES = ("route0", "route0", "route0", "route0")
EXPECTED_ENTRIES = (420, 600, 1320, 1500)
EXPECTED_HEADWAY = 1800


def check_canonical_timetable() -> None:
    services_path = SCENE_DIR / "services.json"
    try:
        data = json.loads(services_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        sys.exit(f"assignment timetable mismatch: cannot read {services_path}: {exc}")

    services = data.get("services")
    if not isinstance(services, list):
        sys.exit("assignment timetable mismatch: services must be an array")
    if len(services) != len(EXPECTED_IDS):
        sys.exit(f"assignment timetable mismatch: expected {len(EXPECTED_IDS)} services, got {len(services)}")

    for index, service in enumerate(services):
        if not isinstance(service, dict):
            sys.exit(f"assignment timetable mismatch at service {index}: expected an object")
        if service.get("id") != EXPECTED_IDS[index]:
            sys.exit(f"assignment timetable mismatch at service {index}: id {service.get('id')!r}, "
                     f"expected {EXPECTED_IDS[index]!r}")
        if service.get("route") != EXPECTED_ROUTES[index]:
            sys.exit(f"assignment timetable mismatch for {EXPECTED_IDS[index]}: route {service.get('route')!r}, "
                     f"expected {EXPECTED_ROUTES[index]!r}")
        if service.get("entry_time_seconds") != EXPECTED_ENTRIES[index]:
            sys.exit(f"assignment timetable mismatch for {EXPECTED_IDS[index]}: entry_time_seconds "
                     f"{service.get('entry_time_seconds')!r}, expected {EXPECTED_ENTRIES[index]}")
        repeat = service.get("repeat")
        if not isinstance(repeat, dict) or repeat.get("headway_seconds") != EXPECTED_HEADWAY:
            actual = repeat.get("headway_seconds") if isinstance(repeat, dict) else None
            sys.exit(f"assignment timetable mismatch for {EXPECTED_IDS[index]}: headway_seconds {actual!r}, "
                     f"expected {EXPECTED_HEADWAY}")


def main() -> None:
    if not SCENE_DIR.exists():
        sys.exit(f"assignment scene not found at {SCENE_DIR}")
    check_canonical_timetable()
    if not SCENE_TOOL.exists():
        sys.exit(f"scene_tool not found at {SCENE_TOOL}. Build first.")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        exported_dir = tmp_dir / "exported"

        print("Validating assignment scene...")
        subprocess.run([str(SCENE_TOOL), "validate", str(SCENE_DIR)], check=True)

        print("Exporting assignment scene...")
        subprocess.run([str(SCENE_TOOL), "export", str(SCENE_DIR), str(exported_dir)], check=True)

        staging_dir = tmp_dir / "staging"
        staging_dir.mkdir()
        (staging_dir / "Output").mkdir()
        input_dir = staging_dir / "Input"
        input_dir.mkdir()
        (input_dir / STAGED_NAME).symlink_to(exported_dir, target_is_directory=True)

        print("Running assignment case...")
        run_case(CASE_ID, cwd=staging_dir, out_base=staging_dir)
        check_movement(CASE_ID, out_base=staging_dir)
        check_station_arrivals(CASE_ID, out_base=staging_dir)


if __name__ == "__main__":
    main()
