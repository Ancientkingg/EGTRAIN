#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
OUT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.log"
SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.png"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

cd "$ROOT/EGTRAIN/QEGTRAIN"
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_VISUAL_POLISH=1 \
QEGTRAIN_E2E_SCREENSHOT="$SHOT" \
"$APP" -n 3 -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$OUT" 2>&1

grep -q "E2E_VISUAL_POLISH_OK" "$OUT"
test -s "$SHOT"
for label in "Planned arrival" "Planned departure" "Simulated arrival" "Simulated departure" "Arrival delay" "Departure delay"; do
	if ! grep -Fqi "$label" "$ROOT/EGTRAIN/QEGTRAIN/app/MainWindow.cpp"; then
		echo "missing timetable label in MainWindow.cpp: $label" >&2
		exit 1
	fi
done
echo "visual polish e2e passed: $SHOT"
