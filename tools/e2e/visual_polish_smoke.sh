#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE_ROOT="$ROOT/EGTRAIN/QEGTRAIN/Scenes"
SCENE="$SCENE_ROOT/Copenhagen"
OUT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.log"
SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-e2e.png"
DENSE_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-dense-e2e.png"
MEDIUM_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-medium-e2e.png"
SELECTED_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-selected-e2e.png"
FOLLOW_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-follow-e2e.png"
CONTEXT_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-context-e2e.png"
COMMAND_BAR_1024_SHOT="${TMPDIR:-/tmp}/qegtrain-command-bar-1024-e2e.png"
COMMAND_BAR_1200_SHOT="${TMPDIR:-/tmp}/qegtrain-command-bar-1200-e2e.png"
COMMAND_BAR_1440_SHOT="${TMPDIR:-/tmp}/qegtrain-command-bar-1440-e2e.png"
DPR2_OUT="${TMPDIR:-/tmp}/qegtrain-visual-polish-dpr2-e2e.log"
DPR2_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-dpr2-e2e.png"
DPR2_COMMAND_BAR_1024_SHOT="${TMPDIR:-/tmp}/qegtrain-command-bar-dpr2-1024-e2e.png"
DPR2_COMMAND_BAR_1200_SHOT="${TMPDIR:-/tmp}/qegtrain-command-bar-dpr2-1200-e2e.png"
DPR2_COMMAND_BAR_1440_SHOT="${TMPDIR:-/tmp}/qegtrain-command-bar-dpr2-1440-e2e.png"
STATION_OUT_BASE="${TMPDIR:-/tmp}/qegtrain-station-overlay-e2e"
STATION_SHOT_BASE="${TMPDIR:-/tmp}/qegtrain-station-overlay-copenhagen"
STATION_DPR2_OUT="${TMPDIR:-/tmp}/qegtrain-station-overlay-e2e-dpr2.log"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

cd "$ROOT/EGTRAIN/QEGTRAIN"
rm -f "$SHOT" "$MEDIUM_SHOT" "$DENSE_SHOT" "$SELECTED_SHOT" "$FOLLOW_SHOT" "$CONTEXT_SHOT" \
	"$DPR2_SHOT" "${DPR2_SHOT%.png}-medium.png" "${DPR2_SHOT%.png}-dense.png" \
	"${DPR2_SHOT%.png}-selected.png" "${DPR2_SHOT%.png}-follow.png"
QT_QPA_PLATFORM=offscreen \
QT_SCALE_FACTOR=1 \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_VISUAL_POLISH=1 \
QEGTRAIN_E2E_SCREENSHOT="$SHOT" \
QEGTRAIN_E2E_DENSE_SCREENSHOT="$DENSE_SHOT" \
QEGTRAIN_E2E_MEDIUM_SCREENSHOT="$MEDIUM_SHOT" \
QEGTRAIN_E2E_SELECTED_SCREENSHOT="$SELECTED_SHOT" \
QEGTRAIN_E2E_FOLLOW_SCREENSHOT="$FOLLOW_SHOT" \
QEGTRAIN_E2E_CONTEXT_SCREENSHOT="$CONTEXT_SHOT" \
QEGTRAIN_E2E_COMMAND_BAR_1024_SCREENSHOT="$COMMAND_BAR_1024_SHOT" \
QEGTRAIN_E2E_COMMAND_BAR_1200_SCREENSHOT="$COMMAND_BAR_1200_SHOT" \
QEGTRAIN_E2E_COMMAND_BAR_1440_SCREENSHOT="$COMMAND_BAR_1440_SHOT" \
	"$APP" --scene "$SCENE" -h 8000 -g 1 -pax 1 -TSM 0 -RC 0 >"$OUT" 2>&1

grep -q "E2E_VISUAL_POLISH_OK" "$OUT"
grep -q "E2E_VISUAL_POLISH_DPR_1.0" "$OUT"
test -s "$SHOT"
test -s "$DENSE_SHOT"
test -s "$MEDIUM_SHOT"
test -s "$SELECTED_SHOT"
test -s "$FOLLOW_SHOT"
test -s "$CONTEXT_SHOT"
test -s "$COMMAND_BAR_1024_SHOT"
test -s "$COMMAND_BAR_1200_SHOT"
test -s "$COMMAND_BAR_1440_SHOT"
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
echo "visual polish e2e passed: $SHOT $MEDIUM_SHOT $DENSE_SHOT $SELECTED_SHOT $FOLLOW_SHOT $CONTEXT_SHOT $COMMAND_BAR_1024_SHOT $COMMAND_BAR_1200_SHOT $COMMAND_BAR_1440_SHOT"

if ! QT_QPA_PLATFORM=offscreen \
	QT_SCALE_FACTOR=2 \
	QEGTRAIN_AUTOSTART=1 \
	QEGTRAIN_E2E_VISUAL_POLISH=1 \
	QEGTRAIN_E2E_SCREENSHOT="$DPR2_SHOT" \
	QEGTRAIN_E2E_DENSE_SCREENSHOT="${DPR2_SHOT%.png}-dense.png" \
	QEGTRAIN_E2E_MEDIUM_SCREENSHOT="${DPR2_SHOT%.png}-medium.png" \
	QEGTRAIN_E2E_SELECTED_SCREENSHOT="${DPR2_SHOT%.png}-selected.png" \
	QEGTRAIN_E2E_FOLLOW_SCREENSHOT="${DPR2_SHOT%.png}-follow.png" \
	QEGTRAIN_E2E_CONTEXT_SCREENSHOT="${DPR2_SHOT%.png}-context.png" \
	QEGTRAIN_E2E_COMMAND_BAR_1024_SCREENSHOT="$DPR2_COMMAND_BAR_1024_SHOT" \
	QEGTRAIN_E2E_COMMAND_BAR_1200_SCREENSHOT="$DPR2_COMMAND_BAR_1200_SHOT" \
	QEGTRAIN_E2E_COMMAND_BAR_1440_SCREENSHOT="$DPR2_COMMAND_BAR_1440_SHOT" \
	"$APP" --scene "$SCENE" -h 8000 -g 1 -pax 1 -TSM 0 -RC 0 >"$DPR2_OUT" 2>&1; then
	cat "$DPR2_OUT" >&2
	exit 2
fi
grep -q "E2E_VISUAL_POLISH_DPR_2.0" "$DPR2_OUT"
grep -q "E2E_VISUAL_POLISH_OK" "$DPR2_OUT"
test -s "$DPR2_SHOT"
test -s "${DPR2_SHOT%.png}-medium.png"
test -s "${DPR2_SHOT%.png}-dense.png"
test -s "${DPR2_SHOT%.png}-selected.png"
test -s "${DPR2_SHOT%.png}-follow.png"
test -s "$DPR2_COMMAND_BAR_1024_SHOT"
test -s "$DPR2_COMMAND_BAR_1200_SHOT"
test -s "$DPR2_COMMAND_BAR_1440_SHOT"
echo "visual polish 2x dpr e2e passed: $DPR2_SHOT"

SCENE_NAMES=(Netherlands Paimpol Copenhagen Milano_Brescia Assignment_Gvc_Gdg_Ut Lebanon)
for case in 1 2 3 4 5 6; do
	scene_name="${SCENE_NAMES[$((case - 1))]}"
	scene_path="$SCENE_ROOT/$scene_name"
	station_out="${STATION_OUT_BASE}-${case}.log"
	if [[ "$case" == "3" ]]; then
		rm -f "${STATION_SHOT_BASE}-dpr1-fit.png" "${STATION_SHOT_BASE}-dpr1-3x.png" "${STATION_SHOT_BASE}-dpr1-12x.png"
		QT_QPA_PLATFORM=offscreen \
		QT_SCALE_FACTOR=1 \
		QEGTRAIN_AUTOSTART=1 \
		QEGTRAIN_E2E_STATION_OVERLAYS=1 \
		QEGTRAIN_E2E_STATION_SCREENSHOT_BASE="${STATION_SHOT_BASE}-dpr1" \
		"$APP" --scene "$scene_path" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$station_out" 2>&1
	else
		QT_QPA_PLATFORM=offscreen \
		QT_SCALE_FACTOR=1 \
		QEGTRAIN_AUTOSTART=1 \
		QEGTRAIN_E2E_STATION_OVERLAYS=1 \
			"$APP" --scene "$scene_path" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$station_out" 2>&1
	fi
	grep -q "E2E_STATION_OVERLAY_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_FIT_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_3X_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_12X_OK" "$station_out"
	grep -q "E2E_STATION_DISPLACED_CLICK_EXACT_OK" "$station_out"
	grep -q "E2E_STATION_DISPLACED_CONTEXT_EXACT_OK" "$station_out"
	grep -q "E2E_STATION_MULTI_SOURCE_BINDING_OK" "$station_out"
	if [[ "$case" == "3" ]]; then
		grep -q "E2E_STATION_OVERLAY_DPR_1.0" "$station_out"
		grep -q "E2E_STATION_DISPLAY_KBHALLEN_OK" "$station_out"
		grep -q "E2E_STATION_BINDING_KBHALLEN_OK" "$station_out"
		test -s "${STATION_SHOT_BASE}-dpr1-fit.png"
		test -s "${STATION_SHOT_BASE}-dpr1-3x.png"
		test -s "${STATION_SHOT_BASE}-dpr1-12x.png"
	fi
	done
echo "station overlay e2e passed: ${STATION_OUT_BASE}-{1,2,3,4,5,6}.log"

rm -f "${STATION_SHOT_BASE}-dpr2-fit.png" "${STATION_SHOT_BASE}-dpr2-3x.png" "${STATION_SHOT_BASE}-dpr2-12x.png"
QT_QPA_PLATFORM=offscreen \
QT_SCALE_FACTOR=2 \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_STATION_OVERLAYS=1 \
QEGTRAIN_E2E_STATION_SCREENSHOT_BASE="${STATION_SHOT_BASE}-dpr2" \
"$APP" --scene "$SCENE_ROOT/Copenhagen" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$STATION_DPR2_OUT" 2>&1
grep -q "E2E_STATION_OVERLAY_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_.*_FIT_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_.*_3X_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_.*_12X_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_DISPLACED_CLICK_EXACT_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_DISPLACED_CONTEXT_EXACT_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_MULTI_SOURCE_BINDING_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_DPR_2.0" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_DISPLAY_KBHALLEN_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_BINDING_KBHALLEN_OK" "$STATION_DPR2_OUT"
test -s "${STATION_SHOT_BASE}-dpr2-fit.png"
test -s "${STATION_SHOT_BASE}-dpr2-3x.png"
test -s "${STATION_SHOT_BASE}-dpr2-12x.png"
echo "station overlay Copenhagen DPR2 passed: ${STATION_DPR2_OUT}"

"$ROOT/tools/e2e/scene_render_smoke.sh"
"$ROOT/tools/e2e/legacy_import_smoke.sh"
