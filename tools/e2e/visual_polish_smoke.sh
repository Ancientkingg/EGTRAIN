#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
OUT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.log"
SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.png"
DENSE_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-dense-e2e.png"
FOLLOW_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-follow-e2e.png"
CONTEXT_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-context-e2e.png"
DPR2_OUT="${TMPDIR:-/tmp}/qegtrain-visual-polish-dpr2-e2e.log"
DPR2_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-dpr2-e2e.png"
STATION_OUT_BASE="${TMPDIR:-/tmp}/qegtrain-station-overlay-e2e"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

cd "$ROOT/EGTRAIN/QEGTRAIN"
rm -f "$CONTEXT_SHOT"
QT_QPA_PLATFORM=offscreen \
QT_SCALE_FACTOR=1 \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_VISUAL_POLISH=1 \
QEGTRAIN_E2E_SCREENSHOT="$SHOT" \
QEGTRAIN_E2E_DENSE_SCREENSHOT="$DENSE_SHOT" \
QEGTRAIN_E2E_FOLLOW_SCREENSHOT="$FOLLOW_SHOT" \
QEGTRAIN_E2E_CONTEXT_SCREENSHOT="$CONTEXT_SHOT" \
"$APP" -n 3 -h 8000 -g 1 -pax 1 -TSM 0 -RC 0 >"$OUT" 2>&1

grep -q "E2E_VISUAL_POLISH_OK" "$OUT"
grep -q "E2E_VISUAL_POLISH_DPR_1.0" "$OUT"
test -s "$SHOT"
test -s "$DENSE_SHOT"
test -s "$FOLLOW_SHOT"
test -s "$CONTEXT_SHOT"
for label in "Planned arrival" "Planned departure" "Simulated arrival" "Simulated departure" "Arrival delay" "Departure delay"; do
	if ! grep -Fqi "$label" "$ROOT/EGTRAIN/QEGTRAIN/diagrams/TimetableTableWindow.cpp"; then
		echo "missing timetable label in TimetableTableWindow.cpp: $label" >&2
		exit 1
	fi
done
if grep -Fq 'name="actionShow_Graph"' "$ROOT/EGTRAIN/QEGTRAIN/app/MainWindow.ui"; then
	echo "dead Show Graph action still present in MainWindow.ui" >&2
	exit 1
fi
echo "visual polish e2e passed: $SHOT $DENSE_SHOT $FOLLOW_SHOT $CONTEXT_SHOT"

QT_QPA_PLATFORM=offscreen \
QT_SCALE_FACTOR=2 \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_VISUAL_POLISH=1 \
QEGTRAIN_E2E_SCREENSHOT="$DPR2_SHOT" \
QEGTRAIN_E2E_DENSE_SCREENSHOT="${DPR2_SHOT%.png}-dense.png" \
QEGTRAIN_E2E_FOLLOW_SCREENSHOT="${DPR2_SHOT%.png}-follow.png" \
QEGTRAIN_E2E_CONTEXT_SCREENSHOT="${DPR2_SHOT%.png}-context.png" \
"$APP" -n 3 -h 8000 -g 1 -pax 1 -TSM 0 -RC 0 >"$DPR2_OUT" 2>&1
grep -q "E2E_VISUAL_POLISH_DPR_2.0" "$DPR2_OUT"
grep -q "E2E_VISUAL_POLISH_OK" "$DPR2_OUT"
test -s "$DPR2_SHOT"
echo "visual polish 2x dpr e2e passed: $DPR2_SHOT"

for case in 1 2 3 4; do
	station_out="${STATION_OUT_BASE}-${case}.log"
	QT_QPA_PLATFORM=offscreen \
	QT_SCALE_FACTOR=1 \
	QEGTRAIN_E2E_STATION_OVERLAYS=1 \
	"$APP" -n "$case" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$station_out" 2>&1
	grep -q "E2E_STATION_OVERLAY_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_FIT_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_3X_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_12X_OK" "$station_out"
	done
echo "station overlay e2e passed: ${STATION_OUT_BASE}-{1,2,3,4}.log"
