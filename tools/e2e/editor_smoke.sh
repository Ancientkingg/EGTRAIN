#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE="${1:-$ROOT/EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut}"
ALT_SCENE="${QEGTRAIN_E2E_SCENE_ALT:-$ROOT/EGTRAIN/QEGTRAIN/Scenes/Copenhagen}"
OUT="${TMPDIR:-/tmp}/qegtrain-editor-smoke-e2e"
LOG="${TMPDIR:-/tmp}/qegtrain-editor-smoke-e2e.log"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi
if [[ ! -d "$ALT_SCENE" ]]; then
	echo "Alternate scene not found: $ALT_SCENE" >&2
	exit 1
fi

if ! ctest --test-dir "$ROOT/build" -R '^test_scene' --output-on-failure --no-tests=error; then
	echo "scene tests failed" >&2
	exit 1
fi
echo "scene tests passed"

mkdir -p "$OUT"

# capture the app exit code without aborting so we can require BOTH a clean exit
# and the pass token; a crash after printing the token must still fail the smoke
set +e
QEGTRAIN_E2E_EDITOR_SMOKE=1 \
QEGTRAIN_E2E_SCENE="$SCENE" \
QEGTRAIN_E2E_SCENE_ALT="$ALT_SCENE" \
QEGTRAIN_E2E_OUT="$OUT" \
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
"$APP" -n 3 -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1
APP_EXIT=$?
set -e

required_markers=(
	E2E_EDITOR_TRAIN_UNIT_OK
	E2E_EDITOR_COMPOSITION_OK
	E2E_EDITOR_SERVICE_OK
	E2E_EDITOR_TIMETABLE_OK
	E2E_EDITOR_INCIDENT_OK
	E2E_EDITOR_VALIDATION_OK
	E2E_EDITOR_SAVE_RELOAD_OK
)
missing_markers=()
for marker in "${required_markers[@]}"; do
	if ! grep -q "$marker" "$LOG"; then
		missing_markers+=("$marker")
	fi
done

if [[ "$APP_EXIT" -eq 0 ]] && [[ "${#missing_markers[@]}" -eq 0 ]] && grep -q "E2E_EDITOR_SMOKE_OK" "$LOG"; then
	echo "editor smoke passed"
else
	echo "editor smoke failed (app exit $APP_EXIT)" >&2
	if [[ "${#missing_markers[@]}" -gt 0 ]]; then
		echo "missing markers: ${missing_markers[*]}" >&2
	fi
	echo "--- log tail ---" >&2
	tail -20 "$LOG" >&2
	exit 1
fi
