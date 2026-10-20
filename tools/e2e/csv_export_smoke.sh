#!/usr/bin/env bash
# Run a case study to completion and export every result view as CSV, then check
# the files have the documented headers, real rows, and no partial output.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="${QEGTRAIN_APP:-$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN}"
SCENE="$ROOT/EGTRAIN/QEGTRAIN/Scenes/Paimpol"
OUTDIR="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e"
RUN_OUTPUT="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e-run"
LOG="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e.log"
# Known block endpoints: every block of this scene has a length in infrastructure.json.
ASSIGNMENT_SCENE="$ROOT/EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut"
ASSIGNMENT_OUTDIR="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e-assignment"
ASSIGNMENT_RUN_OUTPUT="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e-assignment-run"
ASSIGNMENT_LOG="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e-assignment.log"
UNRELATED_OUTDIR="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e-unrelated"
UNRELATED_RUN_OUTPUT="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e-unrelated-run"
UNRELATED_LOG="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e-unrelated.log"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

rm -rf "$OUTDIR" "$RUN_OUTPUT" "$ASSIGNMENT_OUTDIR" "$ASSIGNMENT_RUN_OUTPUT" "$UNRELATED_OUTDIR" "$UNRELATED_RUN_OUTPUT"
mkdir -p "$OUTDIR" "$ASSIGNMENT_OUTDIR" "$UNRELATED_OUTDIR"
UNRELATED_SCENE="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-csv-unrelated-scene.XXXXXX")"
trap 'rm -rf "$UNRELATED_SCENE"' EXIT
# Temporary regression fixture only: the second service has no shared node/station anchors
# and no planned stops. Its calculated envelopes cannot form any chart series on route0.
python3 - "$ASSIGNMENT_SCENE" "$UNRELATED_SCENE" <<'PY_FIXTURE'
import json
import os
import shutil
import sys

source, target = sys.argv[1:]
shutil.copytree(source, target, dirs_exist_ok=True)
for name in ("services.json", "stations.json"):
    path = os.path.join(target, name)
    with open(path, encoding="utf-8") as stream:
        document = json.load(stream)
    if name == "services.json":
        document["services"] = document["services"][:2]
        for service in document["services"]:
            service.pop("repeat", None)
        document["services"][1]["route"] = "route1"
        document["services"][1]["stops"] = []
    else:
        for station in document["stations"]:
            station["platforms"] = [platform for platform in station["platforms"]
                                    if all(node.startswith("B0.") for node in platform["nodes"])]
    with open(path, "w", encoding="utf-8") as stream:
        json.dump(document, stream, indent=4)
        stream.write("\n")
PY_FIXTURE

cd "$ROOT/EGTRAIN/QEGTRAIN"
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_OUTPUT_DIR="${QEGTRAIN_OUTPUT_DIR:-$RUN_OUTPUT}" \
QEGTRAIN_E2E_EXPORT_DIR="$OUTDIR" \
"$APP" --scene "$SCENE" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1

grep -q "E2E_CSV_EXPORT_OK" "$LOG"

# Two trains have calculated envelopes from about 690 s; at 800 s the second one has three.
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_OUTPUT_DIR="${QEGTRAIN_OUTPUT_DIR:-$ASSIGNMENT_RUN_OUTPUT}" \
QEGTRAIN_E2E_EXPORT_DIR="$ASSIGNMENT_OUTDIR" \
"$APP" --scene "$ASSIGNMENT_SCENE" -h 800 -g 1 -pax 0 -TSM 0 -RC 0 >"$ASSIGNMENT_LOG" 2>&1

grep -q "E2E_CSV_EXPORT_OK" "$ASSIGNMENT_LOG"
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_OUTPUT_DIR="${QEGTRAIN_OUTPUT_DIR:-$UNRELATED_RUN_OUTPUT}" \
QEGTRAIN_E2E_EXPORT_DIR="$UNRELATED_OUTDIR" \
"$APP" --scene "$UNRELATED_SCENE" -h 800 -g 1 -pax 0 -TSM 0 -RC 0 >"$UNRELATED_LOG" 2>&1

grep -q "E2E_CSV_EXPORT_OK" "$UNRELATED_LOG"
if [[ ! -s "$ASSIGNMENT_OUTDIR/blocking_time.csv" ]]; then
	echo "missing or empty export: $ASSIGNMENT_OUTDIR/blocking_time.csv" >&2
	exit 1
fi

# These result views always have data after a completed run.
for name in trajectory timetable run_summary delay_analysis; do
	if [[ ! -s "$OUTDIR/$name.csv" ]]; then
		echo "missing or empty export: $OUTDIR/$name.csv" >&2
		exit 1
	fi
	if [[ ! -s "$OUTDIR/$name.csv.provenance.json" ]]; then
		echo "missing or empty provenance sidecar: $OUTDIR/$name.csv.provenance.json" >&2
		exit 1
	fi
done

# The blocking-time export also carries the visible planned reference layer.
if [[ -s "$OUTDIR/blocking_time.csv" ]]; then
	[[ -s "$OUTDIR/blocking_time.csv.provenance.json" ]] \
		|| { echo "missing blocking_time.csv provenance sidecar" >&2; exit 1; }
	head -n1 "$OUTDIR/blocking_time.csv" | grep -q "^Train,Block,Occupation start\[s\]" \
		|| { echo "blocking_time.csv header mismatch" >&2; exit 1; }
	grep -q ",planned reference," "$OUTDIR/blocking_time.csv" \
		|| { echo "blocking_time.csv missing planned reference rows" >&2; exit 1; }
fi

# Headers must match the documented schema.
head -n1 "$OUTDIR/trajectory.csv" | grep -q "^Train,Operating code,Service ID,Occurrence,Time\[s\],Position\[m\],Speed\[m/s\],Power\[kW\],Tractive effort\[kN\],Energy\[kWh\],Block" \
	|| { echo "trajectory.csv header mismatch" >&2; exit 1; }
head -n1 "$OUTDIR/timetable.csv" | grep -q "^Train,Station,Journey order,Operating code,Planned arrival\[s\]" \
	|| { echo "timetable.csv header mismatch" >&2; exit 1; }
head -n1 "$OUTDIR/run_summary.csv" | grep -q "^Train,Operating code,Performance \[%\],Applied maximum speed \[km/h\],Start\[s\],End\[s\],Travel time\[s\]" \
	|| { echo "run_summary.csv header mismatch" >&2; exit 1; }
head -n1 "$OUTDIR/delay_analysis.csv" | grep -q "^Service,Occurrence,Train,Operating code,Station,Journey order,Call,Event,Planned\[s\],Simulated\[s\],Delay\[s\],Class,Status" \
	|| { echo "delay_analysis.csv header mismatch" >&2; exit 1; }

# The delay analysis lists the arrival of every timetable row with the same delay, and a missing time is never a zero delay.
python3 - "$OUTDIR" <<'PY_DELAY'
import csv
import os
import sys

output = sys.argv[1]


def load(name):
    with open(os.path.join(output, name), newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


timetable = {(row["Train"], row["Journey order"]): row for row in load("timetable.csv")}
analysis = load("delay_analysis.csv")
assert timetable and len(analysis) == len(timetable), \
    f"delay_analysis.csv has {len(analysis)} rows for {len(timetable)} timetable rows"
assert {(row["Train"], row["Journey order"]) for row in analysis} == set(timetable), \
    "delay_analysis.csv and timetable.csv list other train calls"
for row in analysis:
    source = timetable[row["Train"], row["Journey order"]]
    where = f"{row['Train']} at {row['Station']}"
    assert row["Event"] == "Arrival" and row["Station"] == source["Station"], where
    assert row["Planned[s]"] == source["Planned arrival[s]"] and row["Simulated[s]"] == source["Simulated arrival[s]"], where
    assert row["Delay[s]"] == source["Arrival delay[s]"], f"{where}: delay differs from timetable.csv"
    if row["Delay[s]"] == "":
        assert row["Class"] == "Not available" and row["Status"] != "Available", f"{where}: a missing delay is not marked"
        assert not (row["Planned[s]"] and row["Simulated[s]"]), f"{where}: both times exist but the delay is missing"
    else:
        delay = float(row["Delay[s]"])
        expected = "Early" if delay < 0 else "Late" if delay > 0 else "On time"
        assert row["Class"] == expected and row["Status"] == "Available", f"{where}: class {row['Class']} for delay {delay}"
PY_DELAY
if [[ -s "$OUTDIR/capacity_analysis.csv" ]]; then
	[[ -s "$OUTDIR/capacity_analysis.csv.provenance.json" ]] \
		|| { echo "missing capacity_analysis.csv provenance sidecar" >&2; exit 1; }
	head -n1 "$OUTDIR/capacity_analysis.csv" | grep -q "^Record type,Leader,Follower,Identity,Operating code,Original reference\[s\].*Cycle time\[s\],Period\[s\],Cycle / period \[%\],Section,Reference label,Reference source" \
		|| { echo "capacity_analysis.csv header mismatch" >&2; exit 1; }
	for record in summary pair compression critical; do
		grep -q "^$record," "$OUTDIR/capacity_analysis.csv" \
			|| { echo "capacity_analysis.csv missing $record records" >&2; exit 1; }
	done
	awk -F, '
NR == 1 {
	for (i = 1; i <= NF; ++i) column[$i] = i
	next
}
NR == 2 {
	if ($(column["Cycle time[s]"]) == "" || $(column["Period[s]"]) == "" ||
		$(column["Cycle / period [%]"]) == "" || $(column["Section"]) == "")
		exit 1
}
' "$OUTDIR/capacity_analysis.csv" \
		|| { echo "capacity_analysis.csv summary lacks cycle, period, percentage, or section" >&2; exit 1; }
else
	grep -q "E2E_CAPACITY_EXPORT_UNAVAILABLE" "$LOG" \
		|| { echo "capacity export was skipped without an explicit unavailable marker" >&2; exit 1; }
fi
if [[ -s "$OUTDIR/area_capacity.csv" ]]; then
	[[ -s "$OUTDIR/area_capacity.csv.provenance.json" ]] \
		|| { echo "missing area_capacity.csv provenance sidecar" >&2; exit 1; }
	head -n1 "$OUTDIR/area_capacity.csv" | grep -q "^Record type,Area,Traffic,Period\[s\],Resource,Trains,Occupations,Occupation\[s\],Union\[s\],Rate \[% of period\],Governing," \
		|| { echo "area_capacity.csv header mismatch" >&2; exit 1; }
	for record in summary resource; do
		grep -q "^$record," "$OUTDIR/area_capacity.csv" \
			|| { echo "area_capacity.csv missing $record records" >&2; exit 1; }
	done
else
	grep -q "E2E_AREA_CAPACITY_EXPORT_UNAVAILABLE" "$LOG" \
		|| { echo "area capacity export was skipped without an explicit unavailable marker" >&2; exit 1; }
fi

python3 - "$SCENE" "$OUTDIR" <<'PY'
import json
import os
import re
import sys

scene, output = map(os.path.abspath, sys.argv[1:])
artifacts = ["trajectory.csv", "timetable.csv", "run_summary.csv", "delay_analysis.csv"]
artifacts += [name for name in ("blocking_time.csv", "capacity_analysis.csv", "area_capacity.csv")
              if os.path.exists(os.path.join(output, name))]
for name in artifacts:
    with open(os.path.join(output, name + ".provenance.json"), encoding="utf-8") as stream:
        document = json.load(stream)
    assert document["schema_version"] == 1
    assert document["artifact"] == {"kind": "csv", "file_name": name}
    run = document["run"]
    assert run["case_name"] == "Paimpol"
    assert run["scene_schema_version"] == 1
    assert run["applied_scenario"] == "baseline"
    assert run["base_time_seconds"] == 7 * 3600 + 10 * 60 + 40
    assert run["duration_seconds"] == 8000
    assert run["timestep_seconds"] == 1
    assert run["buffer_seconds"] == 0
    assert run["recovery_percent"] == 0
    assert run["pax_mode"] == run["tsm_mode"] == run["route_choice_mode"] == 0
    assert run["random_seed"] == 789350715
    snapshot = run["input"]
    assert snapshot["kind"] == "directory"
    assert snapshot["path"] == scene
    assert snapshot["dirty"] is False
    assert snapshot["reproducible"] is True
    assert snapshot["status"] == "reproducible"
    assert snapshot["reason"] == ""
    assert re.fullmatch(r"[0-9a-f]{64}", snapshot["sha256"])
    occurrences = run["selected_occurrences"]
    assert occurrences
    assert all(item["service_id"] and item["occurrence"] >= 1 for item in occurrences)
PY

# The blocking-time export lists the block sections of the reference route after the other rows, and every
# calculated envelope that sits on one of them names it. The reference route is the route of the first service.
python3 - "$SCENE" "$OUTDIR" "$ASSIGNMENT_SCENE" "$ASSIGNMENT_OUTDIR" <<'PY'
import csv
import json
import os
import sys

paimpol_scene, paimpol_output, assignment_scene, assignment_output = sys.argv[1:]
SECTION = "block section extent"
ENVELOPE = "calculated blocking envelope"
UNMAPPED = "unmapped blocking envelope"


def load(scene, name):
    with open(os.path.join(scene, name), encoding="utf-8") as stream:
        return json.load(stream)


def service_route(scene, service_index):
    """Return the id of a service and the block ids of its route."""
    service = load(scene, "services.json")["services"][service_index]
    for route in load(scene, "signalling.json")["routes"]:
        if route["id"] == service["route"]:
            return service["id"], route["blocks"]
    raise AssertionError(f"{scene}: route {service['route']} is not in signalling.json")


def check_export(label, scene, output):
    """Structure shared by both scenes; returns the section rows, the envelope rows and all rows."""
    with open(os.path.join(output, "blocking_time.csv"), newline="", encoding="utf-8") as stream:
        table = list(csv.reader(stream))
    header = table[0]
    assert header[-1] == "Reference section", f"{label}: header does not end with Reference section"
    assert all(len(record) == len(header) for record in table), f"{label}: a row is not as wide as the header"
    rows = [dict(zip(header, record)) for record in table[1:]]
    blocks = service_route(scene, 0)[1]
    sections = [row for row in rows if row["Layer"] == SECTION]
    count = len(blocks)
    assert len(sections) == count, f"{label}: {len(sections)} section rows for {count} blocks"
    assert [row["Reference section"] for row in sections] == [str(i) for i in range(1, count + 1)], \
        f"{label}: section rows are not numbered 1 to {count} in order"
    first = next(i for i, row in enumerate(rows) if row["Layer"] == SECTION)
    assert all(row["Layer"] == SECTION for row in rows[first:]), f"{label}: section rows do not come last"
    assert all(row["Segment type"] == "block section" and not row["Train"] and row["Start X[km]"] and row["End X[km]"]
               for row in sections), f"{label}: section rows are incomplete"
    for before, after in zip(sections, sections[1:]):
        assert abs(float(before["End X[km]"]) - float(after["Start X[km]"])) < 1e-6, \
            f"{label}: sections {before['Reference section']} and {after['Reference section']} do not touch"
    envelopes = [row for row in rows if row["Layer"] == ENVELOPE]
    assert envelopes, f"{label}: no calculated envelopes"
    for row in envelopes:
        if not row["Reference section"]:
            continue
        order = int(row["Reference section"])
        assert 1 <= order <= count, f"{label}: envelope names section {order} of {count}"
        section = sections[order - 1]
        if row["Block"] == section["Block"]:
            assert abs(float(row["Start X[km]"]) - float(section["Start X[km]"])) < 1e-6 \
                and abs(float(row["End X[km]"]) - float(section["End X[km]"])) < 1e-6, \
                f"{label}: envelope of {row['Block']} differs from its section"
    return sections, envelopes, rows


# Assignment_Gvc_Gdg_Ut: every train runs on route0, so every section is known from infrastructure.json.
sections, envelopes, rows = check_export("Assignment export", assignment_scene, assignment_output)
blocks = service_route(assignment_scene, 0)[1]
assert [row["Block"].replace("@", "") for row in sections] == blocks, "Assignment sections are not the blocks of route0"
infrastructure = load(assignment_scene, "infrastructure.json")
lengths = {block["id"]: block["length_km"] for block in infrastructure["blocks"]}
track = next(block["track"] for block in infrastructure["blocks"] if block["id"] == blocks[0])
position = min(node["x_km"] for node in infrastructure["nodes"] if node["track"] == track)
for block, row in zip(blocks, sections):
    assert abs(float(row["Start X[km]"]) - position) < 1e-6, f"{block} starts at {row['Start X[km]']}, not {position}"
    position += lengths[block]
    assert abs(float(row["End X[km]"]) - position) < 1e-6, f"{block} ends at {row['End X[km]']}, not {position}"
assert not [row for row in rows if row["Layer"] == UNMAPPED], "Assignment has an unmapped envelope"
assert len({row["Train"] for row in envelopes}) >= 2, "Assignment export has envelopes for fewer than two trains"
order_of = {row["Block"]: row["Reference section"] for row in sections}
assert all(row["Reference section"] == order_of.get(row["Block"]) for row in envelopes), \
    "an Assignment envelope does not name the section of its block"

# Paimpol: the all-stops service sets the reference route. The express service runs a route with other blocks.
assert os.path.exists(os.path.join(paimpol_output, "blocking_time.csv")), "the Paimpol run did not write blocking_time.csv"
sections, envelopes, rows = check_export("Paimpol export", paimpol_scene, paimpol_output)
assert any(row["Reference section"] for row in envelopes), "no Paimpol envelope names a section"
express_id, express = service_route(paimpol_scene, 1)
unmapped = [row for row in rows if row["Layer"] == UNMAPPED]
assert all(row["Train"].startswith(express_id + "-") and not row["Position[km]"] and not row["Start X[km]"]
           and not row["End X[km]"] and not row["Reference section"] for row in unmapped), \
    "an unmapped Paimpol envelope carries a coordinate or belongs to another service"
# The projection does not extrapolate: these express blocks lie before the first or after the last node both routes share.
assert sorted(row["Block"] for row in unmapped) == ["@0-B0@", "@1-B0@", "@122-B0@", "@123-B0@", "@124-B0@"], \
    "unexpected unmapped Paimpol envelopes"
shown = [row["Block"] for row in rows if row["Train"].startswith(express_id + "-") and row["Layer"] in (ENVELOPE, UNMAPPED)]
assert sorted(shown) == sorted(f"@{block}@" for block in express), "an express envelope is neither shown nor unmapped"
PY

# Exercise the provider installed on the actual blocking-time window, including its train filter.
python3 - "$OUTDIR" "$ASSIGNMENT_OUTDIR" "$UNRELATED_OUTDIR" <<'PY_WINDOW'
import csv
import os
import sys

for output in sys.argv[1:]:
    documents = {}
    for scope in ("all", "one", "none"):
        path = os.path.join(output, f"blocking_time_window_{scope}.csv")
        with open(path, newline="", encoding="utf-8") as stream:
            table = list(csv.reader(stream))
        assert all(len(row) == len(table[0]) for row in table), path
        documents[scope] = [dict(zip(table[0], row)) for row in table[1:]]
    physical = lambda rows: [row for row in rows if row["Segment type"] == "block section"]
    sections = physical(documents["all"])
    assert sections and sections == physical(documents["one"]) == physical(documents["none"]), output
    assert len({row["Reference section"] for row in sections}) == len(sections), "duplicated physical occurrences"
    assert all(not row["Train"] for row in sections), "sections became fake train rows"
    assert all(row["Segment type"] == "block section" for row in documents["none"]), "hidden trains exported"
    assert len({row["Train"] for row in documents["one"] if row["Train"]}) == 1, "single-train window export"
    assert len({row["Train"] for row in documents["all"] if row["Train"]}) >= 2, "multi-train window export"
    for row in sections:
        if row["Layer"] == "block section extent (geometry missing)":
            assert not row["Start X[km]"] and not row["End X[km]"] and not row["Position[km]"]
# With no chart series or planned events for the unrelated train, its id must still
# reach the actual provider from the train filter. Compare with the retained raw rows.
output = sys.argv[3]
with open(os.path.join(output, "blocking_time.csv"), newline="", encoding="utf-8") as stream:
    retained = list(csv.DictReader(stream))
unmapped = [row for row in retained if row["Layer"] == "unmapped blocking envelope"]
assert unmapped and all(row["Train"].startswith("S19825-") for row in unmapped), "fixture lacks wholly unmapped train"
assert unmapped == [row for row in documents["all"] if row["Train"].startswith("S19825-")], \
    "actual window silently dropped the wholly unmapped train"
assert not any(row["Train"].startswith("S19825-") for row in documents["one"] + documents["none"]), \
    "hidden unmapped train was exported"
print("actual blocking-time window provider passed: all/one/no visible trains, sections once, wholly unrelated route retained")
PY_WINDOW

# Trajectory and timetable must carry data rows, not just a header.
traj_rows=$(($(wc -l < "$OUTDIR/trajectory.csv") - 1))
tt_rows=$(($(wc -l < "$OUTDIR/timetable.csv") - 1))
if (( traj_rows < 1 )); then echo "trajectory.csv has no data rows" >&2; exit 1; fi
if (( tt_rows < 1 )); then echo "timetable.csv has no data rows" >&2; exit 1; fi

# The integrator records the effort and power for the interval ending at a row,
# so the power uses that train's preceding speed sample.  Check that retained
# relationship in kW; allow 0.05 kW plus 0.1% for CSV decimal formatting.
awk -F, '
function abs(value) { return value < 0 ? -value : value }
NR == 1 {
	for (i = 1; i <= NF; ++i)
		column[$i] = i
	if (!("Operating code" in column) || !("Service ID" in column) || !("Occurrence" in column) ||
		!("Speed[m/s]" in column) || !("Power[kW]" in column) || !("Tractive effort[kN]" in column)) {
		print "trajectory.csv identity or force header missing" > "/dev/stderr"
		schema = 0
		exit 1
	}
	schema = 1
	next
}
{
	train = $(column["Train"])
	time = $(column["Time[s]"])
	effort = $(column["Tractive effort[kN]"])
	speed = $(column["Speed[m/s]"])
	power = $(column["Power[kW]"])
	if (train == "" || time == "" || effort == "" || speed == "" || power == "")
		next
	if (effort > 0.001) positive = 1
	if (effort < -0.001) negative = 1
	if (abs(effort) <= 0.001) zero = 1
	if (abs(speed) > 0.001 && (train in previousSpeed) && time > previousTime[train] && abs(previousSpeed[train]) > 0.001) {
		expected = effort * previousSpeed[train]
		tolerance = 0.05 + 0.001 * abs(power)
		if (abs(expected - power) > tolerance) {
			if (!mismatch)
				print "force/power mismatch on trajectory row " NR ": " expected " kW vs " power " kW" > "/dev/stderr"
			mismatch = 1
		}
	}
	previousSpeed[train] = speed
	previousTime[train] = time
}
END {
	if (!schema || mismatch || !positive || !zero || !negative) {
		if (!positive || !zero || !negative)
			print "Paimpol trajectory lacks required force signs: positive=" positive ", zero=" zero ", negative=" negative > "/dev/stderr"
		exit 1
	}
}' "$OUTDIR/trajectory.csv" || exit 1

# The run summary always ends with the network totals row.
grep -q "^Network total," "$OUTDIR/run_summary.csv" \
	|| { echo "run_summary.csv missing network totals row" >&2; exit 1; }

echo "csv export e2e passed: $traj_rows trajectory rows, $tt_rows timetable rows in $OUTDIR"
