#!/usr/bin/env python3
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENE_DIR = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut"
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
    print("PASS assignment canonical timetable")


if __name__ == "__main__":
    main()
