#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="${QEGTRAIN_APP:-$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN}"
OUT="${TMPDIR:-/tmp}/qegtrain-scene-render-e2e.log"
SHOT="${TMPDIR:-/tmp}/qegtrain-scene-render-e2e.png"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app is not built" >&2
	exit 1
fi

if [[ $# -gt 0 ]]; then
	SCENE="$1"
else
	SCENE="$ROOT/EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut"
fi

rm -f "$SHOT"
# The check turns View > Passengers on and off, which the program remembers in its settings.
SETTINGS_DIR="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-scene-render-settings.XXXXXX")"
trap 'rm -rf "$SETTINGS_DIR"' EXIT
cd "$ROOT/EGTRAIN/QEGTRAIN"
QEGTRAIN_E2E_SETTINGS_DIR="$SETTINGS_DIR" \
QEGTRAIN_E2E_SCENE_RUN=1 \
QEGTRAIN_E2E_SCENE="$SCENE" \
QEGTRAIN_E2E_SCREENSHOT="$SHOT" \
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
"$APP" --scene "$SCENE" -h 600 -g 1 -pax 0 >"$OUT" 2>&1 &
APP_PID=$!

# macOS has no timeout binary; a hung run must not block the caller
DEADLINE=$((SECONDS + 180))
while kill -0 "$APP_PID" 2>/dev/null; do
	if [[ $SECONDS -ge $DEADLINE ]]; then
		kill -9 "$APP_PID" 2>/dev/null || true
		echo "scene render e2e timed out" >&2
		echo "--- log tail ---" >&2
		tail -5 "$OUT" >&2
		exit 1
	fi
	sleep 2
done

fail_render() {
	echo "scene render e2e failed: $1" >&2
	echo "--- log tail ---" >&2
	tail -20 "$OUT" >&2
	exit 1
}

grep -q "E2E_SCENE_RENDER_OK" "$OUT" || fail_render "E2E_SCENE_RENDER_OK is missing"
# Before the run, Follow is off and says why. The default scene has services, so the case has to run
# first; a scene given on the command line can also be one without trains.
if [[ $# -gt 0 ]]; then
	grep -Eq "E2E_FOLLOW_NO_(RUN|SERVICES)_OK" "$OUT" || fail_render "E2E_FOLLOW_NO_RUN_OK or E2E_FOLLOW_NO_SERVICES_OK is missing"
else
	grep -q "E2E_FOLLOW_NO_RUN_OK" "$OUT" || fail_render "E2E_FOLLOW_NO_RUN_OK is missing"
fi
# View > Passengers is disabled for a case without passengers, and turns the passenger display off and on for one with passengers.
if [[ -f "$SCENE/passengers.json" ]]; then
	grep -q "E2E_PASSENGERS_TOGGLE_OK" "$OUT" || fail_render "E2E_PASSENGERS_TOGGLE_OK is missing"
else
	grep -q "E2E_PASSENGERS_DISABLED_OK" "$OUT" || fail_render "E2E_PASSENGERS_DISABLED_OK is missing"
fi
test -s "$SHOT" || fail_render "no screenshot at $SHOT"
echo "scene render e2e passed: $SHOT"
# The default scene has no passengers; the one with passengers runs next.
if [[ $# -eq 0 ]]; then
	bash "$ROOT/tools/e2e/scene_render_smoke.sh" "$ROOT/EGTRAIN/QEGTRAIN/Scenes/Paimpol"
fi
