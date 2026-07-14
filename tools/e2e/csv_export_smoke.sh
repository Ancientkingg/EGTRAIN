#!/usr/bin/env bash
# Run a case study to completion and export every result view as CSV, then check
# the files have the documented headers, real rows, and no partial output.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
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
"$APP" -n 2 -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1

grep -q "E2E_CSV_EXPORT_OK" "$LOG"

# These result views always have data after a completed run.
for name in trajectory timetable run_summary; do
	if [[ ! -s "$OUTDIR/$name.csv" ]]; then
		echo "missing or empty export: $OUTDIR/$name.csv" >&2
		exit 1
	fi
done

# Blocking-time data is only present when the case study defines complete
# blocking times, so validate its header only when the file was written.
if [[ -s "$OUTDIR/blocking_time.csv" ]]; then
	head -n1 "$OUTDIR/blocking_time.csv" | grep -q "^Train,Block,Occupation start\[s\]" \
		|| { echo "blocking_time.csv header mismatch" >&2; exit 1; }
fi

# Headers must match the documented schema.
head -n1 "$OUTDIR/trajectory.csv" | grep -q "^Train,Time\[s\],Position\[m\],Speed\[m/s\],Power\[kW\],Energy\[kWh\],Block" \
	|| { echo "trajectory.csv header mismatch" >&2; exit 1; }
head -n1 "$OUTDIR/timetable.csv" | grep -q "^Train,Station,Journey order,Planned arrival\[s\]" \
	|| { echo "timetable.csv header mismatch" >&2; exit 1; }
head -n1 "$OUTDIR/run_summary.csv" | grep -q "^Train,Start\[s\],End\[s\],Travel time\[s\]" \
	|| { echo "run_summary.csv header mismatch" >&2; exit 1; }

# Trajectory and timetable must carry data rows, not just a header.
traj_rows=$(($(wc -l < "$OUTDIR/trajectory.csv") - 1))
tt_rows=$(($(wc -l < "$OUTDIR/timetable.csv") - 1))
if (( traj_rows < 1 )); then echo "trajectory.csv has no data rows" >&2; exit 1; fi
if (( tt_rows < 1 )); then echo "timetable.csv has no data rows" >&2; exit 1; fi

# The run summary always ends with the network totals row.
grep -q "^Network total," "$OUTDIR/run_summary.csv" \
	|| { echo "run_summary.csv missing network totals row" >&2; exit 1; }

echo "csv export e2e passed: $traj_rows trajectory rows, $tt_rows timetable rows in $OUTDIR"
