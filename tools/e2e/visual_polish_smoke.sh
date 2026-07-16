#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
OUT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.log"
SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.png"
DENSE_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-dense-e2e.png"
FOLLOW_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-follow-e2e.png"
CONTEXT_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-context-e2e.png"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

cd "$ROOT/EGTRAIN/QEGTRAIN"
rm -f "$CONTEXT_SHOT"
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_VISUAL_POLISH=1 \
QEGTRAIN_E2E_SCREENSHOT="$SHOT" \
QEGTRAIN_E2E_DENSE_SCREENSHOT="$DENSE_SHOT" \
QEGTRAIN_E2E_FOLLOW_SCREENSHOT="$FOLLOW_SHOT" \
QEGTRAIN_E2E_CONTEXT_SCREENSHOT="$CONTEXT_SHOT" \
"$APP" -n 3 -h 8000 -g 1 -pax 1 -TSM 0 -RC 0 >"$OUT" 2>&1

grep -q "E2E_VISUAL_POLISH_OK" "$OUT"
test -s "$SHOT"
test -s "$DENSE_SHOT"
test -s "$FOLLOW_SHOT"
test -s "$CONTEXT_SHOT"
for label in "Planned arrival" "Planned departure" "Simulated arrival" "Simulated departure" "Arrival delay" "Departure delay"; do
	if ! grep -Fqi "$label" "$ROOT/EGTRAIN/QEGTRAIN/app/MainWindow.cpp"; then
		echo "missing timetable label in MainWindow.cpp: $label" >&2
		exit 1
	fi
done
if grep -Fq 'name="actionShow_Graph"' "$ROOT/EGTRAIN/QEGTRAIN/app/MainWindow.ui"; then
	echo "dead Show Graph action still present in MainWindow.ui" >&2
	exit 1
fi
echo "visual polish e2e passed: $SHOT $DENSE_SHOT $FOLLOW_SHOT $CONTEXT_SHOT"
