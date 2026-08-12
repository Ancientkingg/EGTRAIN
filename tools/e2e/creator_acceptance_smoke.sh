#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE_TOOL="$ROOT/build/scene_tool"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi
if [[ ! -x "$SCENE_TOOL" ]]; then
	echo "scene_tool not found or not executable: $SCENE_TOOL" >&2
	exit 1
fi

TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-creator-acceptance.XXXXXX")"
cleanup() {
	local exit_code=$?
	trap - EXIT
	rm -rf "$TMP_ROOT"
	exit "$exit_code"
}
trap cleanup EXIT

FOLDER="$TMP_ROOT/creator-folder"
BUNDLE="$TMP_ROOT/creator.egscene"
UNPACKED="$TMP_ROOT/unpacked-bundle"
OUTPUT="$TMP_ROOT/output"
EXPORTS="$TMP_ROOT/exports"
LOG="$TMP_ROOT/creator-acceptance.log"
mkdir -p "$FOLDER" "$OUTPUT" "$EXPORTS"

set +e
python3 - "$ROOT" "$APP" "$LOG" "$OUTPUT" "$FOLDER" "$BUNDLE" "$EXPORTS" <<'PY'
import os
import signal
import subprocess
import sys
from pathlib import Path

root, app, log, output, folder, bundle, exports = map(Path, sys.argv[1:])
environment = os.environ.copy()
environment.update({
    "QEGTRAIN_E2E_CREATOR_ACCEPTANCE": "1",
    "QEGTRAIN_E2E_OUT": str(output),
    "QEGTRAIN_E2E_CREATOR_FOLDER": str(folder),
    "QEGTRAIN_E2E_CREATOR_BUNDLE": str(bundle),
    "QEGTRAIN_E2E_CREATOR_EXPORT_DIR": str(exports),
    "QT_QPA_PLATFORM": "offscreen",
})
command = [str(app), "-g", "1", "-pax", "1", "-TSM", "0", "-RC", "0"]


def stop_group(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=5)


with log.open("wb") as stream:
    process = subprocess.Popen(
        command,
        cwd=root / "EGTRAIN" / "QEGTRAIN",
        env=environment,
        stdout=stream,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        exit_code = process.wait(timeout=180)
    except subprocess.TimeoutExpired:
        stop_group(process)
        print("creator acceptance app timed out after 180 seconds", file=sys.stderr)
        sys.exit(124)

if exit_code:
    print(f"creator acceptance app exited with {exit_code}", file=sys.stderr)
sys.exit(exit_code)
PY
APP_EXIT=$?
set -e

required_markers=(
	E2E_CREATOR_NEW_CASE_OK
	E2E_CREATOR_INFRASTRUCTURE_OK
	E2E_CREATOR_STATIONS_SIGNALLING_OK
	E2E_CREATOR_ROLLING_STOCK_OK
	E2E_CREATOR_SERVICE_OK
	E2E_CREATOR_SCENARIOS_OK
	E2E_CREATOR_PASSENGER_OK
	E2E_CREATOR_FOLDER_ROUNDTRIP_OK
	E2E_CREATOR_BUNDLE_ROUNDTRIP_OK
	E2E_CREATOR_BASELINE_RUN_OK
	E2E_CREATOR_ENTRANCE_RUN_OK
	E2E_CREATOR_INCIDENT_RUN_OK
	E2E_CREATOR_EXPORTS_OK
	E2E_CREATOR_ACCEPTANCE_OK
)
missing_markers=()
for marker in "${required_markers[@]}"; do
	if ! grep -Fqx "$marker" "$LOG"; then
		missing_markers+=("$marker")
	fi
done
if [[ "$APP_EXIT" -ne 0 || "${#missing_markers[@]}" -ne 0 ]]; then
	echo "creator acceptance smoke failed (app exit $APP_EXIT)" >&2
	if [[ "${#missing_markers[@]}" -gt 0 ]]; then
		echo "missing markers: ${missing_markers[*]}" >&2
	fi
	echo "--- log tail ---" >&2
	tail -40 "$LOG" >&2 || true
	exit 1
fi

"$SCENE_TOOL" validate "$FOLDER"
"$SCENE_TOOL" validate "$BUNDLE"
"$SCENE_TOOL" unpack "$BUNDLE" "$UNPACKED"
"$SCENE_TOOL" validate "$UNPACKED"

python3 - "$FOLDER" "$UNPACKED" "$BUNDLE" "$EXPORTS" <<'PY'
import csv
import json
import math
import re
import sys
from pathlib import Path

folder, unpacked, bundle, exports = map(Path, sys.argv[1:])
EXPECTED_FILES = (
    "scene.json",
    "infrastructure.json",
    "stations.json",
    "signalling.json",
    "rolling_stock.json",
    "services.json",
    "scenarios.json",
    "passengers.json",
)
LEGACY_CASE_NAMES = (
    "netherlands",
    "paimpol",
    "copenhagen",
    "milano_brescia",
    "assignment_gvc_gdg_ut",
    "lebanon",
)
LEGACY_PATH_PARTS = (
    "legacy/",
    "tracklines/",
    "timetable/",
    "traindata/",
    "trains/",
    "routestowrite/",
    "routes/",
    "gui/",
    "passengers/",
)
EXPECTED_OCCURRENCES = {
    ("creator-service", 1),
    ("creator-service", 2),
}


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def number(value, label):
    value = float(value)
    expect(math.isfinite(value), f"{label} is not finite")
    return value


def close(value, expected, label):
    expect(math.isclose(number(value, label), expected, rel_tol=0, abs_tol=1e-9),
           f"{label} expected {expected}, got {value}")


def load_scene(root):
    documents = {}
    for name in EXPECTED_FILES:
        path = root / name
        expect(path.is_file(), f"missing canonical file: {path}")
        try:
            documents[name] = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise AssertionError(f"cannot parse {path}: {error}") from error
    return documents


def reject_legacy_values(value, location="root"):
    if isinstance(value, dict):
        for key, child in value.items():
            key_text = str(key).lower()
            expect(key_text not in {"legacy_root", "import_report"},
                   f"legacy provenance key in canonical data: {location}.{key}")
            reject_legacy_values(child, f"{location}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            reject_legacy_values(child, f"{location}[{index}]")
    elif isinstance(value, str):
        lowered = value.lower()
        expect("legacy_root" not in lowered,
               f"legacy_root in canonical value at {location}")
        expect(not any(name in lowered for name in LEGACY_CASE_NAMES),
               f"legacy case name in canonical value at {location}: {value}")
        expect(not any(part in lowered for part in LEGACY_PATH_PARTS),
               f"legacy path in canonical value at {location}: {value}")
        expect(not re.search(r"(?:^|[/\\])(?:users|private|var|tmp|home)/", lowered),
               f"absolute legacy path in canonical value at {location}: {value}")


def check_scene(documents, label):
    for name, document in documents.items():
        reject_legacy_values(document, f"{label}/{name}")

    scene = documents["scene.json"]
    expect(scene["schema_version"] == 1, f"{label}: scene schema is not v1")
    expect(scene["name"] == "creator_acceptance_case", f"{label}: wrong case name")
    settings = scene["simulation_settings"]
    close(settings["duration_seconds"], 900, f"{label}: duration")

    infrastructure = documents["infrastructure.json"]
    tracks = infrastructure["tracks"]
    expect([track["id"] for track in tracks] == ["creator-main", "creator-yard"],
           f"{label}: tracks are not the authored pair")
    nodes = infrastructure["nodes"]
    expect(len(nodes) == 6, f"{label}: expected 6 nodes")
    expected_nodes = {
        "creator-main-node-0": ("creator-main", 0.0, 0.0),
        "creator-main-node-1": ("creator-main", 1.0, 0.25),
        "creator-main-node-2": ("creator-main", 2.0, 0.0),
        "creator-yard-node-0": ("creator-yard", 0.0, 1.0),
        "creator-yard-node-1": ("creator-yard", 1.5, 1.0),
        "creator-yard-node-2": ("creator-yard", 2.0, 1.0),
    }
    expect({node["id"] for node in nodes} == set(expected_nodes),
           f"{label}: node IDs do not match authored topology")
    for node in nodes:
        track, x, y = expected_nodes[node["id"]]
        expect(node["track"] == track, f"{label}: node track mismatch: {node['id']}")
        close(node["x_km"], x, f"{label}: node x {node['id']}")
        close(node["y_km"], y, f"{label}: node y {node['id']}")

    arcs = infrastructure["arcs"]
    expect(len(arcs) == 4, f"{label}: expected 4 arcs")
    expected_arcs = {
        "creator-main-arc-0": ("creator-main-node-0", "creator-main-node-1", 0, -0.001953125, 22.5),
        "creator-main-arc-1": ("creator-main-node-1", "creator-main-node-2", 1250.5, 0.00390625, 22.5),
        "creator-yard-arc-0": ("creator-yard-node-0", "creator-yard-node-1", 0, 0.0009765625, 18),
        "creator-yard-arc-1": ("creator-yard-node-1", "creator-yard-node-2", 0, 0.0009765625, 18),
    }
    for arc in arcs:
        source, target, curvature, gradient, speed = expected_arcs[arc["id"]]
        expect((arc["from"], arc["to"]) == (source, target),
               f"{label}: arc endpoints mismatch: {arc['id']}")
        close(arc["curvature_radius_m"], curvature, f"{label}: arc curvature {arc['id']}")
        close(arc["gradient_percent"], gradient, f"{label}: arc gradient {arc['id']}")
        close(arc["speed_limit_ms"], speed, f"{label}: arc speed {arc['id']}")

    blocks = infrastructure["blocks"]
    expected_blocks = [
        "creator-main-block-renamed",
        "creator-main-block-1",
        "creator-main-block-2",
        "creator-yard-block-0",
        "creator-yard-block-1",
        "creator-yard-block-2",
    ]
    expect([block["id"] for block in blocks] == expected_blocks,
           f"{label}: blocks are not in authored order")
    expect(all(number(block["length_km"], f"{label}: block length") > 0 for block in blocks),
           f"{label}: block lengths are not positive")

    connections = infrastructure["connections"]
    expect(len(connections) == 1, f"{label}: expected one connection")
    expect(connections[0]["id"] == "creator-switch"
           and connections[0]["from"] == "creator-main-node-1"
           and connections[0]["to"] == "creator-yard-node-1",
           f"{label}: authored switch connection missing")

    stations = documents["stations.json"]["stations"]
    expect(len(stations) == 2, f"{label}: expected two stations")
    expect([station["id"] for station in stations]
           == ["creator-station-a", "creator-station-b"],
           f"{label}: station order mismatch")
    for station in stations:
        expect(len(station["platforms"]) == 1,
               f"{label}: station lacks its authored platform: {station['id']}")
        platform = station["platforms"][0]
        expect(number(platform["length_m"], f"{label}: platform length") > 0
               and number(platform["width_m"], f"{label}: platform width") > 0
               and platform["nodes"],
               f"{label}: platform geometry is not explicit and positive")

    signalling = documents["signalling.json"]
    signals = [signal for signal in signalling["signals"]
               if signal["id"] == "creator-signal"]
    expect(len(signals) == 1 and signals[0]["protected_section"],
           f"{label}: signal lacks an explicit protected section")
    areas = signalling["signalling_areas"]
    expect(len(areas) == 1 and areas[0]["id"] == "creator-signalling-area"
           and areas[0]["level"] == 0 and "track" not in areas[0],
           f"{label}: global conventional signalling area missing")
    close(areas[0]["start_km"], 0, f"{label}: signalling area start")
    close(areas[0]["end_km"], 2, f"{label}: signalling area end")
    routes = [route for route in signalling["routes"] if route["id"] == "creator-route"]
    expect(len(routes) == 1 and len(routes[0]["blocks"]) == 3,
           f"{label}: switched route does not have three sections")
    route_blocks = routes[0]["blocks"]
    expect(route_blocks[0].startswith("@creator-main-block-renamed@")
           and "/" in route_blocks[1]
           and route_blocks[2].startswith("@creator-yard-block-2@"),
           f"{label}: switched route section order is wrong")

    rolling_stock = documents["rolling_stock.json"]
    units = rolling_stock["train_units"]
    expect(len(units) == 2, f"{label}: expected two train units")
    expect([unit["id"] for unit in units] == ["creator-unit-a", "creator-unit-b"],
           f"{label}: train-unit order mismatch")
    physical_fields = (
        "mass_of_traction_unit_kg",
        "mass_of_a_wagon_kg",
        "number_of_wagons",
        "max_deceleration_ms2",
        "max_speed_ms",
        "frontal_area_m2",
        "jerk_ms3",
        "resistance_coefficient",
        "length_m",
    )
    for unit in units:
        physical = unit["physical"]
        expect(all(field in physical for field in physical_fields),
               f"{label}: incomplete physical train-unit data")
        expect(unit["traction_curve"] and all(len(row) >= 5 for row in unit["traction_curve"]),
               f"{label}: missing train-unit traction data")
    close(units[0]["physical"]["mass_of_traction_unit_kg"], 91000,
          f"{label}: focused-save traction-unit mass")
    compositions = rolling_stock["compositions"]
    expect(len(compositions) == 1
           and compositions[0]["units"] == ["creator-unit-a", "creator-unit-b"],
           f"{label}: two-unit composition was not retained in order")

    services = documents["services.json"]["services"]
    expect(len(services) == 1, f"{label}: expected one service")
    service = services[0]
    expect(service["id"] == "creator-service", f"{label}: service ID mismatch")
    expect(service["composition"] == "creator-composition"
           and service["route"] == "creator-route", f"{label}: service references mismatch")
    close(service["performance_percent"], 92, f"{label}: service performance")
    close(service["maximum_speed_kmh"], 100, f"{label}: service maximum speed")
    repeat = service["repeat"]
    expect(repeat["count"] == 3 and repeat["headway_seconds"] == 300
           and repeat["operating_code_step"] == 1,
           f"{label}: repeat/headway/code-step values mismatch")
    stops = service["stops"]
    expect(len(stops) == 2, f"{label}: expected two ordered service stops")
    expect(stops[0]["station"] == "creator-station-a"
           and stops[1]["station"] == "creator-station-b",
           f"{label}: service stop order mismatch")
    close(stops[0]["planned_arrival_seconds"], 180, f"{label}: first arrival")
    close(stops[0]["planned_departure_seconds"], 240, f"{label}: first departure")
    close(stops[1]["planned_arrival_seconds"], 480, f"{label}: second arrival")
    close(stops[1]["planned_departure_seconds"], 540, f"{label}: second departure")
    expect(stops[0]["planned_arrival_seconds"] != stops[0]["planned_departure_seconds"],
           f"{label}: arrival and departure were collapsed")

    scenarios = documents["scenarios.json"]["scenarios"]
    expect([scenario["id"] for scenario in scenarios] == ["baseline", "incident", "entrance"],
           f"{label}: expected baseline, incident, and entrance scenarios")
    incident = scenarios[1]
    expect(any(item["type"] == "signal_failure" and item["target"] == "creator-signal"
               for item in incident["incidents"]), f"{label}: signal failure missing")
    expect(any(item["type"] == "train_breakdown" and item.get("occurrence") == 1
               and item.get("reduced_speed_kmh") == 10
               for item in incident["incidents"]), f"{label}: occurrence-1 reduced-speed breakdown missing")
    entrance_delays = scenarios[2]["entrance_delays"]
    expect(len(entrance_delays) == 1 and entrance_delays[0]["occurrence"] == 2
           and entrance_delays[0]["service"] == "creator-service"
           and entrance_delays[0]["station"] == "creator-station-a",
           f"{label}: occurrence-2 entrance delay binding missing")
    close(entrance_delays[0]["delay_seconds"], 90, f"{label}: entrance delay")

    passengers = documents["passengers.json"]["passengers"]
    expect(len(passengers) == 1, f"{label}: expected one passenger")
    journey = passengers[0]["journeys"][0]
    expect(journey["id"] == "creator-journey" and journey["legs"],
           f"{label}: passenger journey/leg missing")
    leg = journey["legs"][0]
    expect(leg["id"] == "creator-leg" and leg["service"] == "creator-service"
           and leg["occurrence"] == 1 and leg["origin"] == "creator-station-a"
           and leg["destination"] == "creator-station-b",
           f"{label}: passenger leg semantics mismatch")


def check_input(run, scenario, bundle_path):
    expect(run["applied_scenario"] == scenario,
           f"provenance scenario mismatch: expected {scenario}")
    expect(run["case_name"] == "creator_acceptance_case", "provenance case name mismatch")
    expect(run["scene_schema_version"] == 1, "provenance scene schema mismatch")
    expect(run["duration_seconds"] == 900 and run["pax_mode"] == 1
           and run["tsm_mode"] == 0 and run["route_choice_mode"] == 0,
           "provenance run settings mismatch")
    input_data = run["input"]
    expect(input_data["kind"] == "bundle", "provenance input is not the saved bundle")
    expect(Path(input_data["path"]).resolve() == bundle_path.resolve(),
           "provenance input path does not identify the saved bundle")
    expect(input_data["reproducible"] is True and input_data["dirty"] is False
           and input_data["status"] == "reproducible" and input_data["reason"] == "",
           "provenance input is not reproducible")
    expect(re.fullmatch(r"[0-9a-f]{64}", input_data["sha256"] or "") is not None,
           "provenance input hash is not a 64-character lowercase SHA-256")
    occurrences = {(item["service_id"], item["occurrence"])
                   for item in run["selected_occurrences"]}
    expect(occurrences == EXPECTED_OCCURRENCES,
           "provenance does not retain selected occurrences 1 and 2")


def check_sidecar(path, expected_scenario, bundle_path):
    sidecar = Path(str(path) + ".provenance.json")
    expect(sidecar.is_file() and sidecar.stat().st_size > 0,
           f"missing or empty provenance sidecar: {sidecar.name}")
    document = json.loads(sidecar.read_text(encoding="utf-8"))
    expect(document["schema_version"] == 1, f"{sidecar.name}: schema mismatch")
    expect(document["artifact"]["file_name"] == path.name,
           f"{sidecar.name}: artifact filename mismatch")
    expected_kind = "png" if path.suffix.lower() == ".png" else "csv"
    expect(document["artifact"]["kind"] == expected_kind,
           f"{sidecar.name}: artifact kind mismatch")
    check_input(document["run"], expected_scenario, bundle_path)


def check_exports(bundle_path):
    expected = {
        "baseline_time_distance.csv": "baseline",
        "trajectory.csv": "incident",
        "trajectory.png": "incident",
        "timetable.csv": "incident",
        "timetable.png": "incident",
        "blocking_time.csv": "incident",
        "blocking_time.png": "incident",
        "tractive_effort.csv": "incident",
        "tractive_effort.png": "incident",
        "run_summary.csv": "incident",
        "run_summary.png": "incident",
        "capacity_analysis.csv": "incident",
        "capacity_compressed_blocking_time.csv": "incident",
        "capacity_compressed_blocking_time.png": "incident",
        "delay_comparison.csv": "delay",
    }
    for name, scenario in expected.items():
        path = exports / name
        expect(path.is_file() and path.stat().st_size > 0,
               f"missing or empty principal export: {name}")
        if path.suffix.lower() == ".png":
            expect(path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n", f"invalid PNG export: {name}")
        if scenario == "delay":
            sidecar = Path(str(path) + ".provenance.json")
            expect(sidecar.is_file() and sidecar.stat().st_size > 0,
                   f"missing or empty provenance sidecar: {sidecar.name}")
            document = json.loads(sidecar.read_text(encoding="utf-8"))
            expect(document["schema_version"] == 1
                   and document["artifact"] == {"kind": "csv", "file_name": name},
                   f"{name}: delay provenance envelope mismatch")
            check_input(document["baseline_run"], "baseline", bundle_path)
            check_input(document["scenario_run"], "incident", bundle_path)
        else:
            check_sidecar(path, scenario, bundle_path)

    with (exports / "blocking_time.csv").open(newline="", encoding="utf-8") as stream:
        blocking_rows = list(csv.DictReader(stream))
    with (exports / "trajectory.csv").open(newline="", encoding="utf-8") as stream:
        trajectory_rows = list(csv.DictReader(stream))
    selected_train_ids = {
        row["Train"] for row in trajectory_rows
        if row["Service ID"] == "creator-service" and int(row["Occurrence"]) in (1, 2)
    }
    expect(len(selected_train_ids) == 2,
           "trajectory export does not identify both selected service occurrences")
    actual = [row for row in blocking_rows if row["Segment type"] != "planned reference"]
    expect({row["Train"] for row in actual} == selected_train_ids,
           "blocking-time export lacks actual occupations for both selected occurrences")
    expect(all(row["Block"] and row["Occupation start[s]"] and row["Occupation end[s]"]
               for row in actual), "blocking-time actual occupation is incomplete")

    with (exports / "capacity_analysis.csv").open(newline="", encoding="utf-8") as stream:
        capacity_rows = list(csv.DictReader(stream))
    record_types = {row["Record type"] for row in capacity_rows}
    expect({"summary", "pair", "compression"}.issubset(record_types),
           "capacity export lacks summary, pair, or compression evidence")
    summary = next(row for row in capacity_rows if row["Record type"] == "summary")
    expect(number(summary["Cycle time[s]"], "capacity cycle") > 0,
           "capacity cycle is not positive")


for label, root in (("saved folder", folder), ("unpacked bundle", unpacked)):
    check_scene(load_scene(root), label)
check_exports(bundle)
print("creator acceptance canonical semantics and exports passed")
PY

echo "creator acceptance smoke passed"
