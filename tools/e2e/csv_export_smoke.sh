#!/usr/bin/env bash
# Run a case study to completion and export every result view as CSV, then check
# the files have the documented headers, real rows, and no partial output.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE="$ROOT/EGTRAIN/QEGTRAIN/Scenes/Paimpol"
OUTDIR="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e"
LOG="${TMPDIR:-/tmp}/qegtrain-csv-export-e2e.log"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"

cd "$ROOT/EGTRAIN/QEGTRAIN"
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_EXPORT_DIR="$OUTDIR" \
"$APP" --scene "$SCENE" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1

grep -q "E2E_CSV_EXPORT_OK" "$LOG"

# These result views always have data after a completed run.
for name in trajectory timetable run_summary; do
	if [[ ! -s "$OUTDIR/$name.csv" ]]; then
		echo "missing or empty export: $OUTDIR/$name.csv" >&2
		exit 1
	fi
done

# The blocking-time export also carries the visible planned reference layer.
if [[ -s "$OUTDIR/blocking_time.csv" ]]; then
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
if [[ -s "$OUTDIR/capacity_analysis.csv" ]]; then
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
