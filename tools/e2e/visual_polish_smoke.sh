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
STATION_SHOT_BASE="${TMPDIR:-/tmp}/qegtrain-station-overlay"
STATION_DPR2_OUT="${TMPDIR:-/tmp}/qegtrain-station-overlay-e2e-dpr2.log"
COLOR_OUT="${TMPDIR:-/tmp}/qegtrain-visual-polish-color-e2e.log"
COLOR_SHOT="${TMPDIR:-/tmp}/qegtrain-visual-polish-color-e2e.png"
SETTINGS_DIR="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-visual-settings.XXXXXX")"
COLOR_SCENE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-visual-color-scene.XXXXXX")"
SIGNAL_SCENE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-visual-signal-scene.XXXXXX")"
cleanup() {
	local exit_code=$?
	trap - EXIT
	rm -rf "$SETTINGS_DIR" "$COLOR_SCENE_DIR" "$SIGNAL_SCENE_DIR"
	exit "$exit_code"
}
trap cleanup EXIT

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

# The checks of the Follow explanation in a live run, each of which prints its marker when it holds.
FOLLOW_LIVE_MARKERS=(
	E2E_FOLLOW_NOT_ENTERED_OK E2E_FOLLOW_STATUS_WIDTH_OK E2E_FOLLOW_VISIBLE_OK E2E_FOLLOW_CLOCK_OK
	E2E_FOLLOW_ENTERED_OK E2E_FOLLOW_LIST_STABLE_OK E2E_FOLLOW_LAYER_OK E2E_FOLLOW_LIVE_END_OK
)
# The same in the replay of a completed run, and after the case is reset.
FOLLOW_REPLAY_MARKERS=(
	E2E_FOLLOW_REPLAY_BEFORE_OK E2E_FOLLOW_REPLAY_DURING_OK E2E_FOLLOW_REPLAY_LAYER_OK E2E_FOLLOW_SELECT_ON_OK
	E2E_FOLLOW_REPLAY_AFTER_OK E2E_FOLLOW_SELECT_OK E2E_FOLLOW_RESET_OK
)
require_markers() {
	local log="$1" marker
	shift
	for marker in "$@"; do
		if ! grep -q "$marker" "$log"; then
			echo "missing marker $marker in $log" >&2
			exit 1
		fi
	done
}

cd "$ROOT/EGTRAIN/QEGTRAIN"
export QEGTRAIN_E2E_SETTINGS_DIR="$SETTINGS_DIR"
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
grep -q "E2E_OPERATIONAL_TRACK_LIFECYCLE_OK" "$OUT"
grep -q "E2E_VISUAL_POLISH_DPR_1.0" "$OUT"
grep -q "E2E_SELECTION_CUE_FIT_OK" "$OUT"
grep -q "E2E_SELECTION_CUE_DETAIL_OK" "$OUT"
grep -q "E2E_SELECTION_CUE_CLEAR_OK" "$OUT"
require_markers "$OUT" "${FOLLOW_LIVE_MARKERS[@]}"
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

# A copy of the scene in which the first service has a colour: its trains must
# be drawn in that colour, the trains of the other services in the default one.
python3 - "$SCENE" "$COLOR_SCENE_DIR/scene" <<'PY'
import json, shutil, sys
shutil.copytree(sys.argv[1], sys.argv[2])
path = sys.argv[2] + "/services.json"
with open(path) as handle:
    data = json.load(handle)
data["services"][0]["visualization_color"] = "#3c8dd2"
with open(path, "w") as handle:
    json.dump(data, handle, indent=2)
PY
QT_QPA_PLATFORM=offscreen \
QT_SCALE_FACTOR=1 \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_VISUAL_POLISH=1 \
QEGTRAIN_E2E_SCREENSHOT="$COLOR_SHOT" \
QEGTRAIN_E2E_CONTEXT_SCREENSHOT="$COLOR_SCENE_DIR/context.png" \
	"$APP" --scene "$COLOR_SCENE_DIR/scene" -h 8000 -g 1 -pax 1 -TSM 0 -RC 0 >"$COLOR_OUT" 2>&1
grep -q "E2E_VISUAL_POLISH_OK" "$COLOR_OUT"
grep -q "E2E_VISUAL_POLISH_SERVICE_COLOR_OK" "$COLOR_OUT"
if grep -q "E2E_VISUAL_POLISH_SERVICE_COLOR_OK" "$OUT"; then
	echo "the committed scene unexpectedly has service colours" >&2
	exit 1
fi
test -s "$COLOR_SHOT"
echo "service colour e2e passed: $COLOR_SHOT"

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
grep -q "E2E_OPERATIONAL_TRACK_LIFECYCLE_OK" "$DPR2_OUT"
grep -q "E2E_SELECTION_CUE_FIT_OK" "$DPR2_OUT"
grep -q "E2E_SELECTION_CUE_DETAIL_OK" "$DPR2_OUT"
grep -q "E2E_SELECTION_CUE_CLEAR_OK" "$DPR2_OUT"
require_markers "$DPR2_OUT" "${FOLLOW_LIVE_MARKERS[@]}"
test -s "$DPR2_SHOT"
test -s "${DPR2_SHOT%.png}-medium.png"
test -s "${DPR2_SHOT%.png}-dense.png"
test -s "${DPR2_SHOT%.png}-selected.png"
test -s "${DPR2_SHOT%.png}-follow.png"
test -s "$DPR2_COMMAND_BAR_1024_SHOT"
test -s "$DPR2_COMMAND_BAR_1200_SHOT"
test -s "$DPR2_COMMAND_BAR_1440_SHOT"
echo "visual polish 2x dpr e2e passed: $DPR2_SHOT"

COMPLETION_OUT="${TMPDIR:-/tmp}/qegtrain-operational-completion-e2e.log"
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_OPERATIONAL_COMPLETION="$SCENE_ROOT/Assignment_Gvc_Gdg_Ut" \
	"$APP" --scene "$SCENE_ROOT/Assignment_Gvc_Gdg_Ut" -h 600 -g 1 -pax 0 -TSM 0 -RC 0 >"$COMPLETION_OUT" 2>&1
grep -q "E2E_OPERATIONAL_COMPLETION_OK" "$COMPLETION_OUT"
require_markers "$COMPLETION_OUT" "${FOLLOW_REPLAY_MARKERS[@]}"
echo "operational completion and rerun e2e passed"

# Signal heads on three copies of the line fixture with two services: one with a
# level 0 signalling area (heads take stop, caution and proceed and return), one
# in which a signal fails from 400 s to 1000 s, and one without any signalling
# area (every head unavailable). Each run pauses at three steps, then seeks the
# replay. Paimpol has sections that several routes share.
python3 - "$ROOT/EGTRAIN/QEGTRAIN/tests/fixtures/scenes/line" "$SIGNAL_SCENE_DIR" <<'PY'
import json, shutil, sys
for name, scenario, level in (("levels", "baseline", 0), ("failure", "signal-failure-forward", 0), ("none", "baseline", None)):
    target = sys.argv[2] + "/" + name
    shutil.copytree(sys.argv[1], target)
    def edit(file, change):
        with open(target + "/" + file) as handle:
            data = json.load(handle)
        change(data)
        with open(target + "/" + file, "w") as handle:
            json.dump(data, handle, indent=2)
    edit("services.json", lambda data: data.update(services=[s for s in data["services"] if s["id"] in ("F1", "F2")]))
    edit("scenarios.json", lambda data: data.update(default_scenario_id=scenario))
    if level is not None:
        edit("signalling.json", lambda data: data.update(signalling_areas=[{"id": "area.all", "level": level, "start_km": 0.0, "end_km": 16.0}]))
PY
SIGNAL_HEADS_OUT="${TMPDIR:-/tmp}/qegtrain-signal-heads-e2e.log"
for SIGNAL_MODE in levels failure none; do
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_SIGNAL_HEADS="$SIGNAL_MODE" \
QEGTRAIN_E2E_PAUSE_STEPS=100,500,900 \
	"$APP" --scene "$SIGNAL_SCENE_DIR/$SIGNAL_MODE" -h 1500 -g 1 -pax 0 -TSM 0 -RC 0 >"$SIGNAL_HEADS_OUT" 2>&1
grep -q "E2E_SIGNAL_HEADS_OK mode=$SIGNAL_MODE" "$SIGNAL_HEADS_OUT"
done
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_SIGNAL_HEADS=levels \
QEGTRAIN_E2E_PAUSE_STEPS=100,300,500 \
	"$APP" --scene "$SCENE_ROOT/Paimpol" -h 1200 -g 1 -pax 0 -TSM 0 -RC 0 >"$SIGNAL_HEADS_OUT" 2>&1
grep -q "E2E_SIGNAL_HEADS_OK mode=levels" "$SIGNAL_HEADS_OUT"
# Assignment and Lebanon have a level 0 area and heads that no route reaches. A
# scene without any signalling area is the "none" copy of the line fixture above.
for SIGNAL_CASE in Assignment_Gvc_Gdg_Ut:any Lebanon:any; do
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_SIGNAL_HEADS="${SIGNAL_CASE#*:}" \
QEGTRAIN_E2E_PAUSE_STEPS=100,300 \
	"$APP" --scene "$SCENE_ROOT/${SIGNAL_CASE%:*}" -h 600 -g 1 -pax 0 -TSM 0 -RC 0 >"$SIGNAL_HEADS_OUT" 2>&1
grep -q "E2E_SIGNAL_HEADS_OK mode=${SIGNAL_CASE#*:}" "$SIGNAL_HEADS_OUT"
done
echo "signal heads e2e passed"

DISCARD_OUT="${TMPDIR:-/tmp}/qegtrain-operational-discard-e2e.log"
QT_QPA_PLATFORM=offscreen \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_OPERATIONAL_DISCARD=1 \
	"$APP" --scene "$SCENE_ROOT/Assignment_Gvc_Gdg_Ut" -h 600 -g 1 -pax 0 -TSM 0 -RC 0 >"$DISCARD_OUT" 2>&1
grep -q "E2E_OPERATIONAL_DISCARD_OK" "$DISCARD_OUT"
echo "discarded-run preview legend e2e passed"

SCENE_NAMES=(Netherlands Paimpol Copenhagen Milano_Brescia Assignment_Gvc_Gdg_Ut Lebanon)
for case in 1 2 3 4 5 6; do
	scene_name="${SCENE_NAMES[$((case - 1))]}"
	scene_path="$SCENE_ROOT/$scene_name"
	station_out="${STATION_OUT_BASE}-${case}.log"
	shot_base="${STATION_SHOT_BASE}-${scene_name}-dpr1"
	rm -f "${shot_base}-fit.png" "${shot_base}-3x.png" "${shot_base}-12x.png"
	QT_QPA_PLATFORM=offscreen \
	QT_SCALE_FACTOR=1 \
	QEGTRAIN_AUTOSTART=1 \
	QEGTRAIN_E2E_STATION_OVERLAYS=1 \
	QEGTRAIN_E2E_STATION_SCREENSHOT_BASE="$shot_base" \
	"$APP" --scene "$scene_path" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$station_out" 2>&1
	grep -q "E2E_STATION_OVERLAY_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_FIT_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_3X_OK" "$station_out"
	grep -q "E2E_STATION_OVERLAY_.*_12X_OK" "$station_out"
	grep -q "E2E_STATION_ARTWORK_CLICK_OK" "$station_out"
	grep -q "E2E_STATION_NO_GHOST_CONTEXT_OK" "$station_out"
	grep -q "E2E_STATION_MULTI_SOURCE_BINDING_OK" "$station_out"
	grep -q "E2E_STATION_MIN_SIZE_OK" "$station_out"
	grep -q "E2E_STATION_ANCHOR_STABLE_OK" "$station_out"
	grep -q "E2E_STATION_NAME_COLLISION_OK" "$station_out"
	grep -q "E2E_STATION_SCENE_SIZE_OK" "$station_out"
	grep -q "E2E_STATION_NAMES_TOGGLE_OK" "$station_out"
	grep -q "E2E_STATION_SELECTED_NAME_OK" "$station_out"
	test -s "${shot_base}-fit.png"
	test -s "${shot_base}-3x.png"
	test -s "${shot_base}-12x.png"
	if [[ "$case" == "1" ]]; then
		grep -q "E2E_NETHERLANDS_SIGNALS_INDIVIDUAL_OK" "$station_out"
	fi
	if [[ "$case" == "3" ]]; then
		grep -q "E2E_STATION_OVERLAY_DPR_1.0" "$station_out"
		grep -q "E2E_STATION_DISPLAY_KBHALLEN_OK" "$station_out"
		grep -q "E2E_STATION_BINDING_KBHALLEN_OK" "$station_out"
		grep -q "E2E_STATION_SELECTED_NAME_OK was_hidden=1" "$station_out"
	fi
	done
echo "station overlay e2e passed: ${STATION_OUT_BASE}-{1,2,3,4,5,6}.log"

rm -f "${STATION_SHOT_BASE}-Copenhagen-dpr2-fit.png" "${STATION_SHOT_BASE}-Copenhagen-dpr2-3x.png" "${STATION_SHOT_BASE}-Copenhagen-dpr2-12x.png"
QT_QPA_PLATFORM=offscreen \
QT_SCALE_FACTOR=2 \
QEGTRAIN_AUTOSTART=1 \
QEGTRAIN_E2E_STATION_OVERLAYS=1 \
QEGTRAIN_E2E_STATION_SCREENSHOT_BASE="${STATION_SHOT_BASE}-Copenhagen-dpr2" \
"$APP" --scene "$SCENE_ROOT/Copenhagen" -h 8000 -g 1 -pax 0 -TSM 0 -RC 0 >"$STATION_DPR2_OUT" 2>&1
grep -q "E2E_STATION_OVERLAY_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_.*_FIT_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_.*_3X_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_.*_12X_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_ARTWORK_CLICK_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_NO_GHOST_CONTEXT_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_MULTI_SOURCE_BINDING_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_OVERLAY_DPR_2.0" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_DISPLAY_KBHALLEN_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_BINDING_KBHALLEN_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_MIN_SIZE_OK" "$STATION_DPR2_OUT"
grep -q "E2E_STATION_NAME_COLLISION_OK" "$STATION_DPR2_OUT"
test -s "${STATION_SHOT_BASE}-Copenhagen-dpr2-fit.png"
test -s "${STATION_SHOT_BASE}-Copenhagen-dpr2-3x.png"
test -s "${STATION_SHOT_BASE}-Copenhagen-dpr2-12x.png"
echo "station overlay Copenhagen DPR2 passed: ${STATION_DPR2_OUT}"

"$ROOT/tools/e2e/scene_render_smoke.sh"
"$ROOT/tools/e2e/legacy_import_smoke.sh"
