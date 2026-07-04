#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
SCENE="${1:-$ROOT/EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut}"
OUT="${TMPDIR:-/tmp}/qegtrain-scene-render-e2e.log"
SHOT="${TMPDIR:-/tmp}/qegtrain-scene-render-e2e.png"

if [[ ! -x "$APP" ]]; then
	echo "QEGTRAIN app not found or not executable: $APP" >&2
	exit 1
fi

rm -f "$SHOT"
cd "$ROOT/EGTRAIN/QEGTRAIN"
QEGTRAIN_E2E_SCENE_RUN=1 \
QEGTRAIN_E2E_SCENE="$SCENE" \
QEGTRAIN_E2E_SCREENSHOT="$SHOT" \
"$APP" -n 3 -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$OUT" 2>&1 &
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

grep -q "E2E_SCENE_RENDER_OK" "$OUT"
test -s "$SHOT"
echo "scene render e2e passed: $SHOT"
